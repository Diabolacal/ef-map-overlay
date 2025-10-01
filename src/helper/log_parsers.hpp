#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace helper::logs
{
    struct LocalChatEvent
    {
        std::string systemName;
    };

    std::optional<LocalChatEvent> parse_local_chat_line(std::string_view line);

    bool is_combat_log_filename(std::string_view filename);

    std::optional<std::string> combat_log_character_id(std::string_view filename);
}
