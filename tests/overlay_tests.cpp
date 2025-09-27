#include "overlay_schema.hpp"
#include "shared_memory_channel.hpp"

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

    if (failures == 0)
    {
        std::cout << "All overlay tests passed." << std::endl;
        return 0;
    }

    std::cerr << failures << " test(s) failed." << std::endl;
    return 1;
}
