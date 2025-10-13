#pragma once

#include <chrono>
#include <optional>
#include <string>
#include <string_view>

namespace helper::logs
{
    struct LocalChatEvent
    {
        std::string systemName;
    };

    struct CombatDamageEvent
    {
        bool playerDealt{false};
        double amount{0.0};
        std::string counterparty;
        std::chrono::system_clock::time_point timestamp{};
    };

    struct MiningYieldEvent
    {
        double volumeM3{0.0};
        std::string resource;
        std::chrono::system_clock::time_point timestamp{};
    };

    std::optional<LocalChatEvent> parse_local_chat_line(std::string_view line);

    bool is_combat_log_filename(std::string_view filename);

    std::optional<std::string> combat_log_character_id(std::string_view filename);

    std::optional<CombatDamageEvent> parse_combat_damage_line(std::string_view line);

    std::optional<MiningYieldEvent> parse_mining_yield_line(std::string_view line);
}
