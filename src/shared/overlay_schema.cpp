#include "overlay_schema.hpp"

#include <chrono>
#include <stdexcept>

namespace overlay
{
    namespace
    {
        double read_double(const nlohmann::json& obj, const char* key)
        {
            if (!obj.contains(key))
            {
                throw std::invalid_argument(std::string{"Missing numeric field: "} + key);
            }

            const auto& value = obj.at(key);
            if (!value.is_number())
            {
                throw std::invalid_argument(std::string{"Field '"} + key + "' must be numeric");
            }

            return value.get<double>();
        }

        std::string read_string(const nlohmann::json& obj, const char* key)
        {
            if (!obj.contains(key))
            {
                throw std::invalid_argument(std::string{"Missing string field: "} + key);
            }

            const auto& value = obj.at(key);
            if (!value.is_string())
            {
                throw std::invalid_argument(std::string{"Field '"} + key + "' must be a string");
            }

            return value.get<std::string>();
        }

        bool read_bool(const nlohmann::json& obj, const char* key, bool default_value)
        {
            if (!obj.contains(key))
            {
                return default_value;
            }

            const auto& value = obj.at(key);
            if (!value.is_boolean())
            {
                throw std::invalid_argument(std::string{"Field '"} + key + "' must be boolean");
            }

            return value.get<bool>();
        }

        overlay::Vec3f read_vec3(const nlohmann::json& obj, const char* key)
        {
            if (!obj.contains(key))
            {
                throw std::invalid_argument(std::string{"Missing vector field: "} + key);
            }

            const auto& value = obj.at(key);
            if (!value.is_array() || value.size() != 3)
            {
                throw std::invalid_argument(std::string{"Field '"} + key + "' must be an array of 3 numbers");
            }

            overlay::Vec3f vec;
            vec.x = value.at(0).get<float>();
            vec.y = value.at(1).get<float>();
            vec.z = value.at(2).get<float>();
            return vec;
        }

        std::uint64_t read_uint64(const nlohmann::json& obj, const char* key, std::uint64_t default_value)
        {
            if (!obj.contains(key))
            {
                return default_value;
            }

            const auto& value = obj.at(key);
            if (!value.is_number_unsigned())
            {
                throw std::invalid_argument(std::string{"Field '"} + key + "' must be an unsigned integer");
            }

            return value.get<std::uint64_t>();
        }
    }

    OverlayState parse_overlay_state(const nlohmann::json& json)
    {
        OverlayState state;

        if (json.contains("version"))
        {
            state.version = json.at("version").get<int>();
        }

        if (json.contains("generated_at_ms"))
        {
            state.generated_at_ms = json.at("generated_at_ms").get<std::uint64_t>();
        }

        state.heartbeat_ms = read_uint64(json, "heartbeat_ms", state.generated_at_ms);

        if (!json.contains("route"))
        {
            throw std::invalid_argument("Overlay payload must include route array");
        }

        const auto& route = json.at("route");
        if (!route.is_array())
        {
            throw std::invalid_argument("route must be an array");
        }

        state.route.reserve(route.size());
        for (const auto& node : route)
        {
            if (!node.is_object())
            {
                throw std::invalid_argument("route entries must be objects");
            }

            RouteNode entry;
            entry.system_id = read_string(node, "system_id");
            entry.display_name = read_string(node, "display_name");
            entry.distance_ly = read_double(node, "distance_ly");
            entry.via_gate = read_bool(node, "via_gate", false);
            state.route.push_back(std::move(entry));
        }

        if (json.contains("notes"))
        {
            state.notes = json.at("notes").get<std::string>();
        }

        if (json.contains("player_marker") && json.at("player_marker").is_object())
        {
            const auto& markerJson = json.at("player_marker");
            PlayerMarker marker;
            marker.system_id = read_string(markerJson, "system_id");
            marker.display_name = read_string(markerJson, "display_name");
            marker.is_docked = read_bool(markerJson, "is_docked", false);
            state.player_marker = std::move(marker);
        }

        if (json.contains("highlighted_systems") && json.at("highlighted_systems").is_array())
        {
            const auto& items = json.at("highlighted_systems");
            state.highlighted_systems.reserve(items.size());
            for (const auto& raw : items)
            {
                if (!raw.is_object())
                {
                    throw std::invalid_argument("highlighted_systems entries must be objects");
                }
                HighlightedSystem highlight;
                highlight.system_id = read_string(raw, "system_id");
                highlight.display_name = read_string(raw, "display_name");
                highlight.category = read_string(raw, "category");
                if (raw.contains("note") && !raw.at("note").is_null())
                {
                    highlight.note = raw.at("note").get<std::string>();
                }
                state.highlighted_systems.push_back(std::move(highlight));
            }
        }

        if (json.contains("camera_pose") && json.at("camera_pose").is_object())
        {
            const auto& poseJson = json.at("camera_pose");
            CameraPose pose;
            pose.position = read_vec3(poseJson, "position");
            pose.look_at = read_vec3(poseJson, "look_at");
            if (poseJson.contains("up"))
            {
                pose.up = read_vec3(poseJson, "up");
            }
            if (poseJson.contains("fov_degrees"))
            {
                pose.fov_degrees = poseJson.at("fov_degrees").get<float>();
            }
            state.camera_pose = pose;
        }

        if (json.contains("hud_hints") && json.at("hud_hints").is_array())
        {
            const auto& hints = json.at("hud_hints");
            state.hud_hints.reserve(hints.size());
            for (const auto& raw : hints)
            {
                if (!raw.is_object())
                {
                    throw std::invalid_argument("hud_hints entries must be objects");
                }
                HudHint hint;
                hint.id = read_string(raw, "id");
                hint.text = read_string(raw, "text");
                hint.dismissible = read_bool(raw, "dismissible", false);
                hint.active = read_bool(raw, "active", true);
                state.hud_hints.push_back(std::move(hint));
            }
        }

    state.follow_mode_enabled = read_bool(json, "follow_mode_enabled", false);

        if (json.contains("active_route_node_id") && !json.at("active_route_node_id").is_null())
        {
            state.active_route_node_id = json.at("active_route_node_id").get<std::string>();
        }

        state.source_online = read_bool(json, "source_online", true);

        if (json.contains("telemetry") && json.at("telemetry").is_object())
        {
            const auto& telemetryJson = json.at("telemetry");
            TelemetryMetrics metrics;
            if (telemetryJson.contains("combat") && telemetryJson.at("combat").is_object())
            {
                const auto& combatJson = telemetryJson.at("combat");
                CombatTelemetry combat;
                if (combatJson.contains("total_damage_dealt"))
                {
                    combat.total_damage_dealt = combatJson.at("total_damage_dealt").get<double>();
                }
                if (combatJson.contains("total_damage_taken"))
                {
                    combat.total_damage_taken = combatJson.at("total_damage_taken").get<double>();
                }
                if (combatJson.contains("recent_damage_dealt"))
                {
                    combat.recent_damage_dealt = combatJson.at("recent_damage_dealt").get<double>();
                }
                if (combatJson.contains("recent_damage_taken"))
                {
                    combat.recent_damage_taken = combatJson.at("recent_damage_taken").get<double>();
                }
                if (combatJson.contains("recent_window_seconds"))
                {
                    combat.recent_window_seconds = combatJson.at("recent_window_seconds").get<double>();
                }
                if (combatJson.contains("last_event_ms"))
                {
                    combat.last_event_ms = combatJson.at("last_event_ms").get<std::uint64_t>();
                }
                if (combatJson.contains("session_start_ms"))
                {
                    combat.session_start_ms = combatJson.at("session_start_ms").get<std::uint64_t>();
                }
                if (combatJson.contains("session_duration_seconds"))
                {
                    combat.session_duration_seconds = combatJson.at("session_duration_seconds").get<double>();
                }
                
                // Hit quality counters (dealt)
                if (combatJson.contains("miss_dealt"))
                {
                    combat.miss_dealt = combatJson.at("miss_dealt").get<std::uint64_t>();
                }
                if (combatJson.contains("glancing_dealt"))
                {
                    combat.glancing_dealt = combatJson.at("glancing_dealt").get<std::uint64_t>();
                }
                if (combatJson.contains("standard_dealt"))
                {
                    combat.standard_dealt = combatJson.at("standard_dealt").get<std::uint64_t>();
                }
                if (combatJson.contains("penetrating_dealt"))
                {
                    combat.penetrating_dealt = combatJson.at("penetrating_dealt").get<std::uint64_t>();
                }
                if (combatJson.contains("smashing_dealt"))
                {
                    combat.smashing_dealt = combatJson.at("smashing_dealt").get<std::uint64_t>();
                }
                
                // Hit quality counters (taken)
                if (combatJson.contains("miss_taken"))
                {
                    combat.miss_taken = combatJson.at("miss_taken").get<std::uint64_t>();
                }
                if (combatJson.contains("glancing_taken"))
                {
                    combat.glancing_taken = combatJson.at("glancing_taken").get<std::uint64_t>();
                }
                if (combatJson.contains("standard_taken"))
                {
                    combat.standard_taken = combatJson.at("standard_taken").get<std::uint64_t>();
                }
                if (combatJson.contains("penetrating_taken"))
                {
                    combat.penetrating_taken = combatJson.at("penetrating_taken").get<std::uint64_t>();
                }
                if (combatJson.contains("smashing_taken"))
                {
                    combat.smashing_taken = combatJson.at("smashing_taken").get<std::uint64_t>();
                }
                
                metrics.combat = combat;
            }

            if (telemetryJson.contains("mining") && telemetryJson.at("mining").is_object())
            {
                const auto& miningJson = telemetryJson.at("mining");
                MiningTelemetry mining;
                if (miningJson.contains("total_volume_m3"))
                {
                    mining.total_volume_m3 = miningJson.at("total_volume_m3").get<double>();
                }
                if (miningJson.contains("recent_volume_m3"))
                {
                    mining.recent_volume_m3 = miningJson.at("recent_volume_m3").get<double>();
                }
                if (miningJson.contains("recent_window_seconds"))
                {
                    mining.recent_window_seconds = miningJson.at("recent_window_seconds").get<double>();
                }
                if (miningJson.contains("last_event_ms"))
                {
                    mining.last_event_ms = miningJson.at("last_event_ms").get<std::uint64_t>();
                }
                if (miningJson.contains("session_start_ms"))
                {
                    mining.session_start_ms = miningJson.at("session_start_ms").get<std::uint64_t>();
                }
                if (miningJson.contains("session_duration_seconds"))
                {
                    mining.session_duration_seconds = miningJson.at("session_duration_seconds").get<double>();
                }
                if (miningJson.contains("buckets") && miningJson.at("buckets").is_array())
                {
                    const auto& bucketsJson = miningJson.at("buckets");
                    mining.buckets.reserve(bucketsJson.size());
                    for (const auto& bucketJson : bucketsJson)
                    {
                        if (!bucketJson.is_object())
                        {
                            throw std::invalid_argument("Telemetry bucket entries must be objects");
                        }
                        TelemetryBucket bucket;
                        if (bucketJson.contains("id"))
                        {
                            bucket.id = bucketJson.at("id").get<std::string>();
                        }
                        if (bucketJson.contains("label"))
                        {
                            bucket.label = bucketJson.at("label").get<std::string>();
                        }
                        if (bucketJson.contains("session_total"))
                        {
                            bucket.session_total = bucketJson.at("session_total").get<double>();
                        }
                        if (bucketJson.contains("recent_total"))
                        {
                            bucket.recent_total = bucketJson.at("recent_total").get<double>();
                        }
                        mining.buckets.push_back(std::move(bucket));
                    }
                }
                metrics.mining = mining;
            }

            if (telemetryJson.contains("history") && telemetryJson.at("history").is_object())
            {
                const auto& historyJson = telemetryJson.at("history");
                TelemetryHistory history;
                if (historyJson.contains("slice_seconds"))
                {
                    history.slice_seconds = historyJson.at("slice_seconds").get<double>();
                }
                if (historyJson.contains("capacity"))
                {
                    history.capacity = historyJson.at("capacity").get<std::uint32_t>();
                }
                if (historyJson.contains("saturated"))
                {
                    history.saturated = historyJson.at("saturated").get<bool>();
                }
                if (historyJson.contains("reset_markers_ms") && historyJson.at("reset_markers_ms").is_array())
                {
                    const auto& markers = historyJson.at("reset_markers_ms");
                    history.reset_markers_ms.reserve(markers.size());
                    for (const auto& marker : markers)
                    {
                        history.reset_markers_ms.push_back(marker.get<std::uint64_t>());
                    }
                }
                if (historyJson.contains("slices") && historyJson.at("slices").is_array())
                {
                    const auto& slices = historyJson.at("slices");
                    history.slices.reserve(slices.size());
                    for (const auto& sliceJson : slices)
                    {
                        if (!sliceJson.is_object())
                        {
                            throw std::invalid_argument("Telemetry history slice entries must be objects");
                        }
                        TelemetryHistorySlice slice;
                        if (sliceJson.contains("start_ms"))
                        {
                            slice.start_ms = sliceJson.at("start_ms").get<std::uint64_t>();
                        }
                        if (sliceJson.contains("duration_seconds"))
                        {
                            slice.duration_seconds = sliceJson.at("duration_seconds").get<double>();
                        }
                        if (sliceJson.contains("damage_dealt"))
                        {
                            slice.damage_dealt = sliceJson.at("damage_dealt").get<double>();
                        }
                        if (sliceJson.contains("damage_taken"))
                        {
                            slice.damage_taken = sliceJson.at("damage_taken").get<double>();
                        }
                        if (sliceJson.contains("mining_volume_m3"))
                        {
                            slice.mining_volume_m3 = sliceJson.at("mining_volume_m3").get<double>();
                        }
                        history.slices.push_back(std::move(slice));
                    }
                }

                metrics.history = std::move(history);
            }

            if (metrics.combat.has_value() || metrics.mining.has_value() || metrics.history.has_value())
            {
                state.telemetry = std::move(metrics);
            }
        }

        if (state.heartbeat_ms == 0)
        {
            state.heartbeat_ms = state.generated_at_ms;
        }

        if (state.generated_at_ms == 0)
        {
            state.generated_at_ms = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count());
        }

        if (state.heartbeat_ms == 0)
        {
            state.heartbeat_ms = state.generated_at_ms;
        }

        return state;
    }

    nlohmann::json serialize_overlay_state(const OverlayState& state)
    {
        nlohmann::json json;
        json["version"] = state.version;
        json["generated_at_ms"] = state.generated_at_ms;
        json["heartbeat_ms"] = state.heartbeat_ms == 0 ? state.generated_at_ms : state.heartbeat_ms;

        nlohmann::json route = nlohmann::json::array();
        route.get_ref<nlohmann::json::array_t&>().reserve(state.route.size());

        for (const auto& node : state.route)
        {
            route.push_back({
                {"system_id", node.system_id},
                {"display_name", node.display_name},
                {"distance_ly", node.distance_ly},
                {"via_gate", node.via_gate}
            });
        }

        json["route"] = std::move(route);

        if (state.notes.has_value())
        {
            json["notes"] = *state.notes;
        }

        if (state.player_marker.has_value())
        {
            const auto& marker = *state.player_marker;
            json["player_marker"] = {
                {"system_id", marker.system_id},
                {"display_name", marker.display_name},
                {"is_docked", marker.is_docked}
            };
        }

        if (!state.highlighted_systems.empty())
        {
            nlohmann::json highlights = nlohmann::json::array();
            highlights.get_ref<nlohmann::json::array_t&>().reserve(state.highlighted_systems.size());
            for (const auto& highlight : state.highlighted_systems)
            {
                nlohmann::json entry = {
                    {"system_id", highlight.system_id},
                    {"display_name", highlight.display_name},
                    {"category", highlight.category}
                };
                if (highlight.note.has_value())
                {
                    entry["note"] = *highlight.note;
                }
                highlights.push_back(std::move(entry));
            }
            json["highlighted_systems"] = std::move(highlights);
        }

        if (state.camera_pose.has_value())
        {
            const auto& pose = *state.camera_pose;
            const auto to_array = [](const Vec3f& vec) {
                return nlohmann::json::array({vec.x, vec.y, vec.z});
            };
            nlohmann::json poseJson;
            poseJson["position"] = to_array(pose.position);
            poseJson["look_at"] = to_array(pose.look_at);
            poseJson["up"] = to_array(pose.up);
            poseJson["fov_degrees"] = pose.fov_degrees;
            json["camera_pose"] = std::move(poseJson);
        }

        if (!state.hud_hints.empty())
        {
            nlohmann::json hints = nlohmann::json::array();
            hints.get_ref<nlohmann::json::array_t&>().reserve(state.hud_hints.size());
            for (const auto& hint : state.hud_hints)
            {
                hints.push_back({
                    {"id", hint.id},
                    {"text", hint.text},
                    {"dismissible", hint.dismissible},
                    {"active", hint.active}
                });
            }
            json["hud_hints"] = std::move(hints);
        }

        json["follow_mode_enabled"] = state.follow_mode_enabled;

        if (state.active_route_node_id.has_value())
        {
            json["active_route_node_id"] = *state.active_route_node_id;
        }

        json["source_online"] = state.source_online;

        if (state.telemetry.has_value())
        {
            const auto& metrics = *state.telemetry;
            nlohmann::json telemetryJson = nlohmann::json::object();

            if (metrics.combat.has_value())
            {
                const auto& combat = *metrics.combat;
                telemetryJson["combat"] = {
                    {"total_damage_dealt", combat.total_damage_dealt},
                    {"total_damage_taken", combat.total_damage_taken},
                    {"recent_damage_dealt", combat.recent_damage_dealt},
                    {"recent_damage_taken", combat.recent_damage_taken},
                    {"recent_window_seconds", combat.recent_window_seconds},
                    {"last_event_ms", combat.last_event_ms},
                    {"session_start_ms", combat.session_start_ms},
                    {"session_duration_seconds", combat.session_duration_seconds},
                    // Hit quality counters (dealt)
                    {"miss_dealt", combat.miss_dealt},
                    {"glancing_dealt", combat.glancing_dealt},
                    {"standard_dealt", combat.standard_dealt},
                    {"penetrating_dealt", combat.penetrating_dealt},
                    {"smashing_dealt", combat.smashing_dealt},
                    // Hit quality counters (taken)
                    {"miss_taken", combat.miss_taken},
                    {"glancing_taken", combat.glancing_taken},
                    {"standard_taken", combat.standard_taken},
                    {"penetrating_taken", combat.penetrating_taken},
                    {"smashing_taken", combat.smashing_taken}
                };
            }

            if (metrics.mining.has_value())
            {
                const auto& mining = *metrics.mining;
                telemetryJson["mining"] = {
                    {"total_volume_m3", mining.total_volume_m3},
                    {"recent_volume_m3", mining.recent_volume_m3},
                    {"recent_window_seconds", mining.recent_window_seconds},
                    {"last_event_ms", mining.last_event_ms},
                    {"session_start_ms", mining.session_start_ms},
                    {"session_duration_seconds", mining.session_duration_seconds}
                };
                if (!mining.buckets.empty())
                {
                    nlohmann::json buckets = nlohmann::json::array();
                    buckets.get_ref<nlohmann::json::array_t&>().reserve(mining.buckets.size());
                    for (const auto& bucket : mining.buckets)
                    {
                        buckets.push_back({
                            {"id", bucket.id},
                            {"label", bucket.label},
                            {"session_total", bucket.session_total},
                            {"recent_total", bucket.recent_total}
                        });
                    }
                    telemetryJson["mining"]["buckets"] = std::move(buckets);
                }
            }

            if (metrics.history.has_value())
            {
                const auto& history = *metrics.history;
                nlohmann::json historyJson = {
                    {"slice_seconds", history.slice_seconds},
                    {"capacity", history.capacity},
                    {"saturated", history.saturated}
                };

                if (!history.reset_markers_ms.empty())
                {
                    historyJson["reset_markers_ms"] = history.reset_markers_ms;
                }

                if (!history.slices.empty())
                {
                    nlohmann::json slices = nlohmann::json::array();
                    slices.get_ref<nlohmann::json::array_t&>().reserve(history.slices.size());
                    for (const auto& slice : history.slices)
                    {
                        slices.push_back({
                            {"start_ms", slice.start_ms},
                            {"duration_seconds", slice.duration_seconds},
                            {"damage_dealt", slice.damage_dealt},
                            {"damage_taken", slice.damage_taken},
                            {"mining_volume_m3", slice.mining_volume_m3}
                        });
                    }
                    historyJson["slices"] = std::move(slices);
                }

                telemetryJson["history"] = std::move(historyJson);
            }

            if (!telemetryJson.empty())
            {
                json["telemetry"] = std::move(telemetryJson);
            }
        }

        return json;
    }
}
