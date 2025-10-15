#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace overlay
{
    constexpr int schema_version = 4;

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

    struct CombatTelemetry
    {
        double total_damage_dealt{0.0};
        double total_damage_taken{0.0};
        double recent_damage_dealt{0.0};
        double recent_damage_taken{0.0};
        double recent_window_seconds{30.0};
        std::uint64_t last_event_ms{0};
    };

    struct TelemetryBucket
    {
        std::string id;
        std::string label;
        double session_total{0.0};
        double recent_total{0.0};
    };

    struct MiningTelemetry
    {
        double total_volume_m3{0.0};
        double recent_volume_m3{0.0};
        double recent_window_seconds{120.0};
        std::uint64_t last_event_ms{0};
        std::uint64_t session_start_ms{0};
        double session_duration_seconds{0.0};
        std::vector<TelemetryBucket> buckets;
    };

    struct TelemetryHistorySlice
    {
        std::uint64_t start_ms{0};
        double duration_seconds{0.0};
        double damage_dealt{0.0};
        double damage_taken{0.0};
        double mining_volume_m3{0.0};
    };

    struct TelemetryHistory
    {
        double slice_seconds{300.0};
        std::uint32_t capacity{0};
        bool saturated{false};
        std::vector<TelemetryHistorySlice> slices;
        std::vector<std::uint64_t> reset_markers_ms;
    };

    struct TelemetryMetrics
    {
        std::optional<CombatTelemetry> combat;
        std::optional<MiningTelemetry> mining;
        std::optional<TelemetryHistory> history;
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
        std::optional<TelemetryMetrics> telemetry;
    };

    [[nodiscard]] OverlayState parse_overlay_state(const nlohmann::json& json);
    [[nodiscard]] nlohmann::json serialize_overlay_state(const OverlayState& state);
}
