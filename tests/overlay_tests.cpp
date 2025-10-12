#include "overlay_schema.hpp"
#include "shared_memory_channel.hpp"
#include "event_channel.hpp"
#include "helper/log_parsers.hpp"
#include "helper/system_resolver.hpp"
#include "shared/star_catalog.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>

#include <chrono>
#include <iostream>
#include <string>
#include <vector>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>

#include <nlohmann/json.hpp>

namespace
{
    using overlay::OverlayState;
    using overlay::RouteNode;

    template <typename Fn>
    void run_case(const char* name, Fn&& fn, int& failures)
    {
        try
        {
            fn();
            std::cout << "[PASS] " << name << "\n";
        }
        catch (const std::exception& ex)
        {
            std::cerr << "[FAIL] " << name << ": " << ex.what() << "\n";
            ++failures;
        }
        catch (...)
        {
            std::cerr << "[FAIL] " << name << ": unknown exception\n";
            ++failures;
        }
    }

    OverlayState make_sample_state()
    {
        OverlayState state;
        state.generated_at_ms = 123456789ULL;
        state.heartbeat_ms = state.generated_at_ms;
        state.route = {
            RouteNode{"30000001", "Tanoo", 0.0, false},
            RouteNode{"30000003", "Mahnna", 3.47, true}
        };
        state.notes = std::string{"Sample payload"};
        state.follow_mode_enabled = true;
        state.source_online = true;
        state.player_marker = overlay::PlayerMarker{ "30000003", "Mahnna", false };
        overlay::HighlightedSystem highlight;
        highlight.system_id = "30000005";
        highlight.display_name = "Amdim";
        highlight.category = "route";
        highlight.note = std::string{"Next waypoint"};
        state.highlighted_systems.push_back(highlight);
        overlay::CameraPose pose;
        pose.position = overlay::Vec3f{1.0f, 2.0f, 3.0f};
        pose.look_at = overlay::Vec3f{4.0f, 5.0f, 6.0f};
        pose.up = overlay::Vec3f{0.0f, 1.0f, 0.0f};
        pose.fov_degrees = 55.0f;
        state.camera_pose = pose;
        state.hud_hints.push_back(overlay::HudHint{"hint-1", "Press F8 to toggle", true, true});
        state.active_route_node_id = std::string{"30000003"};
        return state;
    }
}

int main()
{
    int failures = 0;

    run_case("overlay schema round-trip", [&](void) {
        auto state = make_sample_state();
        auto json = overlay::serialize_overlay_state(state);
        auto restored = overlay::parse_overlay_state(json);

        if (restored.route.size() != state.route.size())
        {
            throw std::runtime_error("route size mismatch");
        }

        for (std::size_t i = 0; i < state.route.size(); ++i)
        {
            const auto& lhs = state.route[i];
            const auto& rhs = restored.route[i];
            if (lhs.system_id != rhs.system_id || lhs.display_name != rhs.display_name)
            {
                throw std::runtime_error("route entries differ after round-trip");
            }
        }

        if (!restored.notes.has_value() || restored.notes->compare(*state.notes) != 0)
        {
            throw std::runtime_error("notes did not round-trip");
        }

        if (restored.heartbeat_ms != state.heartbeat_ms)
        {
            throw std::runtime_error("heartbeat did not round-trip");
        }

        if (!restored.source_online)
        {
            throw std::runtime_error("source_online flag expected true");
        }
    }, failures);

    run_case("overlay schema validation", [&](void) {
        nlohmann::json json = nlohmann::json::object();
        bool threw = false;
        try
        {
            (void)overlay::parse_overlay_state(json);
        }
        catch (const std::invalid_argument&)
        {
            threw = true;
        }

        if (!threw)
        {
            throw std::runtime_error("expected parse_overlay_state to throw on missing route");
        }
    }, failures);

    run_case("local chat parser extracts system", []() {
        const auto sample = std::string{"[ 2025.09.30 15:07:01 ] Keeper > Channel changed to Local : E78-F01"};
        auto parsed = helper::logs::parse_local_chat_line(sample);
        if (!parsed.has_value())
        {
            throw std::runtime_error("Expected parser to yield a system name");
        }
        if (parsed->systemName != "E78-F01")
        {
            throw std::runtime_error("Unexpected system name parsed");
        }
    }, failures);

    run_case("combat log filename requires character id", []() {
        if (helper::logs::is_combat_log_filename("20250921_132937.txt"))
        {
            throw std::runtime_error("Filename without character id should be rejected");
        }

        if (!helper::logs::is_combat_log_filename("20250921_132937_2112049754.txt"))
        {
            throw std::runtime_error("Expected filename with character id to be recognised");
        }

        auto id = helper::logs::combat_log_character_id("20250921_132937_2112049754.txt");
        if (!id.has_value() || *id != "2112049754")
        {
            throw std::runtime_error("Unexpected character id parsed");
        }
    }, failures);

    run_case("system resolver finds canonical ids", []() {
        helper::logs::SystemResolver resolver;
        auto id = resolver.resolve("A 2560");
        if (!id.has_value() || *id != "30000001")
        {
            throw std::runtime_error("Expected A 2560 to map to 30000001");
        }

        auto also = resolver.resolve("a 2560");
        if (!also.has_value() || *also != "30000001")
        {
            throw std::runtime_error("Resolver should be case-insensitive");
        }

        auto dup = resolver.resolve("D:28NL");
        if (dup.has_value())
        {
            throw std::runtime_error("Resolver should not resolve duplicate system names");
        }
    }, failures);

    run_case("shared memory writer/reader", [&](void) {
        const auto state = make_sample_state();
        const auto payload = overlay::serialize_overlay_state(state).dump();

        overlay::SharedMemoryWriter writer;
        if (!writer.write(payload, static_cast<std::uint32_t>(state.version), state.generated_at_ms))
        {
            throw std::runtime_error("SharedMemoryWriter.write returned false");
        }

        overlay::SharedMemoryReader reader;
        auto snapshot = reader.read();
        if (!snapshot.has_value())
        {
            throw std::runtime_error("SharedMemoryReader.read yielded no data");
        }

        if (snapshot->json_payload != payload)
        {
            throw std::runtime_error("Shared memory payload mismatch");
        }

    if (snapshot->version != static_cast<std::uint32_t>(state.version))
        {
            throw std::runtime_error("Shared memory version mismatch");
        }

        if (snapshot->updated_at_ms != state.generated_at_ms)
        {
            throw std::runtime_error("Shared memory timestamp mismatch");
        }
    }, failures);

    run_case("overlay event queue", [&](void) {
        overlay::OverlayEventWriter writer;
        if (!writer.ensure())
        {
            throw std::runtime_error("Failed to initialize event writer");
        }

        overlay::OverlayEvent event;
        event.type = overlay::OverlayEventType::WaypointAdvanced;
        event.payload = R"({"test":"value"})";
        event.timestamp_ms = 987654321ULL;
        if (!writer.publish(event))
        {
            throw std::runtime_error("Failed to publish event");
        }

        overlay::OverlayEventReader reader;
        auto drained = reader.drain();
        if (drained.events.empty())
        {
            throw std::runtime_error("No events received from queue");
        }

        const auto& received = drained.events.front();
        if (received.type != overlay::OverlayEventType::WaypointAdvanced)
        {
            throw std::runtime_error("Event type mismatch");
        }
        if (received.payload != event.payload)
        {
            throw std::runtime_error("Event payload mismatch");
        }
    }, failures);

    run_case("star catalog loader", []() {
        const std::vector<std::string> names{"Alpha", "Beta"};
        std::vector<std::uint32_t> nameOffsets;
        std::string stringBlob;
        nameOffsets.reserve(names.size());
        for (const auto& name : names)
        {
            nameOffsets.push_back(static_cast<std::uint32_t>(stringBlob.size()));
            stringBlob.append(name);
        }

        std::vector<std::uint8_t> buffer;
        buffer.reserve(44 + names.size() * 36 + stringBlob.size());

        auto append_bytes = [&buffer](const void* data, std::size_t count) {
            const auto* ptr = static_cast<const std::uint8_t*>(data);
            buffer.insert(buffer.end(), ptr, ptr + count);
        };

        auto append_u16 = [&append_bytes](std::uint16_t value) {
            append_bytes(&value, sizeof(value));
        };

        auto append_u32 = [&append_bytes](std::uint32_t value) {
            append_bytes(&value, sizeof(value));
        };

        auto append_f32 = [&append_bytes](float value) {
            append_bytes(&value, sizeof(value));
        };

        const char magic[8] = {'E','F','S','T','A','R','S','1'};
        append_bytes(magic, sizeof(magic));
        append_u16(1);  // version
        append_u16(36); // record size
        append_u32(static_cast<std::uint32_t>(names.size()));

        // bbox min/max
        append_f32(0.0f);
        append_f32(0.0f);
        append_f32(-1.0f);
        append_f32(10.0f);
        append_f32(20.0f);
        append_f32(30.0f);

        append_u32(static_cast<std::uint32_t>(stringBlob.size()));

        const std::array<std::uint32_t, 2> systemIds{42u, 43u};
        const std::array<std::uint32_t, 2> regionIds{7u, 8u};
        const std::array<std::uint32_t, 2> constellationIds{3u, 4u};
        const std::array<std::array<float, 3>, 2> positions{{{1.0f, 2.0f, 3.0f}, {4.0f, 5.0f, 6.0f}}};
        const std::array<float, 2> securities{0.7f, 0.2f};

        for (std::size_t i = 0; i < names.size(); ++i)
        {
            append_u32(systemIds[i]);
            append_u32(regionIds[i]);
            append_u32(constellationIds[i]);
            append_u32(nameOffsets[i]);
            append_u16(static_cast<std::uint16_t>(names[i].size()));
            const std::uint8_t spectral = static_cast<std::uint8_t>(i + 1);
            append_bytes(&spectral, sizeof(spectral));
            const std::uint8_t flags = static_cast<std::uint8_t>(i);
            append_bytes(&flags, sizeof(flags));
            append_f32(positions[i][0]);
            append_f32(positions[i][1]);
            append_f32(positions[i][2]);
            append_f32(securities[i]);
        }

        append_bytes(stringBlob.data(), stringBlob.size());

        auto catalog = overlay::load_star_catalog(buffer);
        if (catalog.version != 1)
        {
            throw std::runtime_error("Unexpected catalog version");
        }
        if (catalog.record_size != 36)
        {
            throw std::runtime_error("Unexpected record size");
        }
        if (catalog.records.size() != names.size())
        {
            throw std::runtime_error("Catalog record count mismatch");
        }

        const auto* alpha = catalog.find_by_system_id(42u);
        if (!alpha)
        {
            throw std::runtime_error("Expected to find system 42");
        }

        auto alphaName = catalog.name_for(*alpha);
        if (alphaName != "Alpha")
        {
            throw std::runtime_error("System 42 name mismatch");
        }

        if (std::abs(alpha->position.x - 1.0f) > 1e-6f || std::abs(alpha->position.y - 2.0f) > 1e-6f)
        {
            throw std::runtime_error("System 42 position mismatch");
        }

        if (catalog.find_by_system_id(999u) != nullptr)
        {
            throw std::runtime_error("Unexpected hit for unknown system");
        }

        if (catalog.name_for(catalog.records.back()) != "Beta")
        {
            throw std::runtime_error("System 43 name mismatch");
        }
    }, failures);

    if (failures == 0)
    {
        std::cout << "All overlay tests passed." << std::endl;
        return 0;
    }

    std::cerr << failures << " test(s) failed." << std::endl;
    return 1;
}
