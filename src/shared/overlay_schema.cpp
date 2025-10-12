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

        return json;
    }
}
