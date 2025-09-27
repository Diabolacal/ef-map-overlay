#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace overlay
{
    constexpr int schema_version = 1;

    struct RouteNode
    {
        std::string system_id;
        std::string display_name;
        double distance_ly = 0.0;
        bool via_gate = false;
    };

    struct OverlayState
    {
        int version = schema_version;
        std::uint64_t generated_at_ms = 0;
        std::vector<RouteNode> route;
        std::optional<std::string> notes;
    };

    [[nodiscard]] OverlayState parse_overlay_state(const nlohmann::json& json);
    [[nodiscard]] nlohmann::json serialize_overlay_state(const OverlayState& state);
}
