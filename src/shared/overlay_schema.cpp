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

        if (state.generated_at_ms == 0)
        {
            state.generated_at_ms = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count());
        }

        return state;
    }

    nlohmann::json serialize_overlay_state(const OverlayState& state)
    {
        nlohmann::json json;
        json["version"] = state.version;
        json["generated_at_ms"] = state.generated_at_ms;

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

        return json;
    }
}
