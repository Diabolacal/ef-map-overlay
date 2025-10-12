#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace overlay
{
    constexpr int schema_version = 2;

    struct Vec3f
    {
        float x{0.0f};
        float y{0.0f};
        float z{0.0f};
    };

    struct CameraPose
    {
        Vec3f position;
        Vec3f look_at;
        Vec3f up{0.0f, 1.0f, 0.0f};
        float fov_degrees{60.0f};
    };

    struct PlayerMarker
    {
        std::string system_id;
        std::string display_name;
        bool is_docked{false};
    };

    struct HighlightedSystem
    {
        std::string system_id;
        std::string display_name;
        std::string category;
        std::optional<std::string> note;
    };

    struct HudHint
    {
        std::string id;
        std::string text;
        bool dismissible{false};
        bool active{true};
    };

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
        std::uint64_t heartbeat_ms = 0;
        std::vector<RouteNode> route;
        std::optional<std::string> notes;
        std::optional<PlayerMarker> player_marker;
        std::vector<HighlightedSystem> highlighted_systems;
        std::optional<CameraPose> camera_pose;
        std::vector<HudHint> hud_hints;
        bool follow_mode_enabled{false};
        std::optional<std::string> active_route_node_id;
        bool source_online{true};
    };

    [[nodiscard]] OverlayState parse_overlay_state(const nlohmann::json& json);
    [[nodiscard]] nlohmann::json serialize_overlay_state(const OverlayState& state);
}
