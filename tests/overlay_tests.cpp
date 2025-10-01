#include "overlay_schema.hpp"
#include "shared_memory_channel.hpp"
#include "event_channel.hpp"
#include "helper/log_parsers.hpp"
#include "helper/system_resolver.hpp"

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
        state.route = {
            RouteNode{"30000001", "Tanoo", 0.0, false},
            RouteNode{"30000003", "Mahnna", 3.47, true}
        };
        state.notes = std::string{"Sample payload"};
        state.follow_mode_enabled = true;
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

    if (failures == 0)
    {
        std::cout << "All overlay tests passed." << std::endl;
        return 0;
    }

    std::cerr << failures << " test(s) failed." << std::endl;
    return 1;
}
