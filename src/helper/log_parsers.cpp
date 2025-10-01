#include "log_parsers.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <regex>
#include <vector>

namespace helper::logs
{
    namespace
    {
        std::string trim_copy(std::string_view input)
        {
            auto begin = input.begin();
            auto end = input.end();
            while (begin != end && std::isspace(static_cast<unsigned char>(*begin)))
            {
                ++begin;
            }
            if (begin == end)
            {
                return std::string{};
            }
            do
            {
                --end;
            } while (end != begin && std::isspace(static_cast<unsigned char>(*end)));
            return std::string(begin, end + 1);
        }

        std::string to_lower_copy(std::string_view input)
        {
            std::string output;
            output.reserve(input.size());
            for (const auto ch : input)
            {
                output.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
            }
            return output;
        }

        bool is_all_digits(std::string_view value)
        {
            if (value.empty())
            {
                return false;
            }
            return std::all_of(value.begin(), value.end(), [](unsigned char ch) {
                return std::isdigit(ch) != 0;
            });
        }
    }

    std::optional<LocalChatEvent> parse_local_chat_line(std::string_view line)
    {
        static const std::regex pattern{R"(Channel\s+changed\s+to\s+Local\s*:\s*(.+))", std::regex::icase};
        std::smatch match;
        std::string owned(line);
        if (!std::regex_search(owned, match, pattern))
        {
            return std::nullopt;
        }

        if (match.size() < 2)
        {
            return std::nullopt;
        }

        auto system = trim_copy(match[1].str());
        if (system.empty())
        {
            return std::nullopt;
        }

        if (!system.empty() && static_cast<unsigned char>(system.front()) == 0xEF && system.size() >= 3)
        {
            const auto bom = std::array<unsigned char, 3>{0xEF, 0xBB, 0xBF};
            if (std::equal(bom.begin(), bom.end(), reinterpret_cast<const unsigned char*>(system.data()), reinterpret_cast<const unsigned char*>(system.data()) + 3))
            {
                system.erase(0, 3);
                system = trim_copy(system);
            }
        }

        if (system.empty())
        {
            return std::nullopt;
        }

        return LocalChatEvent{std::move(system)};
    }

    bool is_combat_log_filename(std::string_view filename)
    {
        if (filename.empty())
        {
            return false;
        }

        const auto pos = filename.find_last_of("/\\");
        if (pos != std::string_view::npos)
        {
            filename.remove_prefix(pos + 1);
        }

        if (filename.size() < 4)
        {
            return false;
        }

        const auto lower = to_lower_copy(filename);
        if (lower.size() < 4 || lower.substr(lower.size() - 4) != ".txt")
        {
            return false;
        }

        const auto stem = filename.substr(0, filename.size() - 4);
        std::vector<std::string> parts;
        std::size_t start = 0;
        while (start <= stem.size())
        {
            const auto idx = stem.find('_', start);
            if (idx == std::string::npos)
            {
                parts.emplace_back(stem.substr(start));
                break;
            }
            parts.emplace_back(stem.substr(start, idx - start));
            start = idx + 1;
        }

        if (parts.size() < 3)
        {
            return false;
        }

        if (parts[0].size() != 8 || parts[1].size() != 6)
        {
            return false;
        }

        if (!is_all_digits(parts[0]) || !is_all_digits(parts[1]) || !is_all_digits(parts[2]))
        {
            return false;
        }

        return true;
    }

    std::optional<std::string> combat_log_character_id(std::string_view filename)
    {
        if (!is_combat_log_filename(filename))
        {
            return std::nullopt;
        }

        const auto pos = filename.find_last_of("/\\");
        if (pos != std::string_view::npos)
        {
            filename.remove_prefix(pos + 1);
        }

        const auto stem = filename.substr(0, filename.size() - 4);
        const auto lastUnderscore = stem.find_last_of('_');
        if (lastUnderscore == std::string_view::npos)
        {
            return std::nullopt;
        }

        auto id = std::string(stem.substr(lastUnderscore + 1));
        if (!is_all_digits(id))
        {
            return std::nullopt;
        }

        return id;
    }
}
