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
        std::uint64_t session_start_ms{0};
        double session_duration_seconds{0.0};
        
        // Hit quality counters (dealt)
        std::uint64_t miss_dealt{0};
        std::uint64_t glancing_dealt{0};
        std::uint64_t standard_dealt{0};
        std::uint64_t penetrating_dealt{0};
        std::uint64_t smashing_dealt{0};
        
        // Hit quality counters (taken)
        std::uint64_t miss_taken{0};
        std::uint64_t glancing_taken{0};
        std::uint64_t standard_taken{0};
        std::uint64_t penetrating_taken{0};
        std::uint64_t smashing_taken{0};
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

    struct PscanNode
    {
        std::string id;                 // Smart assembly ID
        std::string name;               // Node display name (e.g., "Network Node")
        std::string type;               // Assembly type (e.g., "NetworkNode")
        std::string owner_name;         // Owner display name
        double distance_m{0.0};         // Distance from player's deployed structure (meters)
    };

    struct PscanData
    {
        std::string system_id;          // Solar system where scan was performed
        std::string system_name;        // Solar system display name
        std::uint64_t scanned_at_ms{0}; // Timestamp of scan
        std::vector<PscanNode> nodes;   // Network nodes found, sorted by distance
    };

    struct RouteNode
    {
        std::string system_id;
        std::string display_name;
        double distance_ly = 0.0;
        bool via_gate = false;          // True if using a Stargate
        bool via_smart_gate = false;    // True if using a Smart Gate
        int planet_count = 0;           // Number of planets in this system
        int network_nodes = 0;          // Count of NetworkNode Smart Assembly infrastructure in this system
        int route_position = 0;         // Position in the full route (1-based, e.g., "hop 5 of 12")
        int total_route_hops = 0;       // Total number of hops (systems) in the route
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
        
        // Session tracking state
        bool visited_systems_tracking_enabled{false};
        bool has_active_session{false};
        std::optional<std::string> active_session_id;
        
        // Bookmark capability state (for overlay UI conditional rendering)
        bool authenticated{false};  // User has connected wallet
        std::optional<std::string> tribe_id;   // Tribe ID (null if not in tribe or CloneBank86)
        std::optional<std::string> tribe_name; // Tribe display name
        
        // Proximity scan data
        std::optional<PscanData> pscan_data;
    };

    [[nodiscard]] OverlayState parse_overlay_state(const nlohmann::json& json);
    [[nodiscard]] nlohmann::json serialize_overlay_state(const OverlayState& state);
}
