#include "log_parsers.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <ctime>
#include <iomanip>
#include <regex>
#include <sstream>
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

        std::optional<std::chrono::system_clock::time_point> parse_timestamp(std::string_view line)
        {
            const auto open = line.find('[');
            const auto close = line.find(']');
            if (open == std::string_view::npos || close == std::string_view::npos || close <= open + 1)
            {
                return std::nullopt;
            }

            auto raw = trim_copy(line.substr(open + 1, close - open - 1));
            if (raw.size() < 19)
            {
                return std::nullopt;
            }

            std::tm tm{};
            std::istringstream iss(raw.substr(0, 19));
            iss >> std::get_time(&tm, "%Y.%m.%d %H:%M:%S");
            if (iss.fail())
            {
                return std::nullopt;
            }

#if defined(_WIN32)
            const auto epoch = _mkgmtime(&tm);
#elif defined(__unix__) || defined(__APPLE__)
            const auto epoch = timegm(&tm);
#else
            const auto epoch = std::mktime(&tm);
#endif
            if (epoch == -1)
            {
                return std::nullopt;
            }

            return std::chrono::system_clock::from_time_t(epoch);
        }

        double parse_number(std::string_view token)
        {
            std::string sanitized;
            sanitized.reserve(token.size());
            for (const auto ch : token)
            {
                if (ch != ',')
                {
                    sanitized.push_back(ch);
                }
            }

            try
            {
                return std::stod(sanitized);
            }
            catch (...)
            {
                return 0.0;
            }
        }

        std::string lowercase_copy(std::string_view input)
        {
            std::string out;
            out.reserve(input.size());
            for (const auto ch : input)
            {
                out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
            }
            return out;
        }

        std::string_view trim_view(std::string_view value)
        {
            while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())) != 0)
            {
                value.remove_prefix(1);
            }
            while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())) != 0)
            {
                value.remove_suffix(1);
            }
            return value;
        }

        std::string strip_markup(std::string_view value)
        {
            std::string output;
            output.reserve(value.size());
            bool inTag = false;
            for (const char ch : value)
            {
                if (ch == '<')
                {
                    inTag = true;
                    continue;
                }
                if (ch == '>')
                {
                    inTag = false;
                    continue;
                }
                if (!inTag)
                {
                    output.push_back(ch);
                }
            }
            return output;
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

    std::optional<CombatDamageEvent> parse_combat_damage_line(std::string_view line)
    {
        constexpr std::string_view kCombatToken{"(combat)"};
        if (line.find(kCombatToken) == std::string_view::npos)
        {
            return std::nullopt;
        }

        const auto timestamp = parse_timestamp(line).value_or(std::chrono::system_clock::now());

        const auto strippedStorage = strip_markup(line);
        std::string_view strippedView = strippedStorage;
        auto lower = lowercase_copy(strippedView);

        const auto combatPos = lower.find(kCombatToken);
        if (combatPos != std::string::npos)
        {
            strippedView.remove_prefix(combatPos + kCombatToken.size());
            lower = lowercase_copy(strippedView);
        }

        strippedView = trim_view(strippedView);
        lower = lowercase_copy(strippedView);

        if (lower.empty())
        {
            return std::nullopt;
        }

        // Handle miss events first (they don't have damage amounts)
        if (lower.find("miss") != std::string::npos)
        {
            bool playerDealtMiss = false;
            std::string counterpartyMiss;
            
            // Check for "you miss" or "your ... misses"
            if (lower.find("you miss") != std::string::npos || lower.find("your ") != std::string::npos)
            {
                playerDealtMiss = true;
                const auto youMissPos = lower.find("you miss ");
                if (youMissPos != std::string::npos)
                {
                    // Extract target after "you miss"
                    const auto targetStart = youMissPos + std::string("you miss ").size();
                    counterpartyMiss = std::string(trim_view(strippedView.substr(targetStart)));
                }
                else
                {
                    // Look for "your ... misses ..."
                    const auto yourPos = lower.find("your ");
                    const auto missesPos = lower.find(" misses ", yourPos);
                    if (missesPos != std::string::npos)
                    {
                        const auto targetStart = missesPos + std::string(" misses ").size();
                        counterpartyMiss = std::string(trim_view(strippedView.substr(targetStart)));
                    }
                }
            }
            // Check for "X misses you" or "X's ... misses you"
            else if (lower.find(" misses you") != std::string::npos || lower.find(" miss you") != std::string::npos)
            {
                playerDealtMiss = false;
                const auto missYouPos = lower.find(" miss");  // Find start of " misses you" or " miss you"
                if (missYouPos != std::string::npos)
                {
                    counterpartyMiss = std::string(trim_view(strippedView.substr(0, missYouPos)));
                }
            }
            
            if (!counterpartyMiss.empty())
            {
                CombatDamageEvent event;
                event.playerDealt = playerDealtMiss;
                event.amount = 0.0;  // Misses have no damage
                event.counterparty = std::move(counterpartyMiss);
                event.quality = HitQuality::Miss;
                event.timestamp = timestamp;
                return event;
            }
        }

        const auto cleanup_name = [](std::string_view name) {
            name = trim_view(name);
            const auto dash = name.find(" -");
            if (dash != std::string::npos)
            {
                name.remove_suffix(name.size() - dash);
            }
            return std::string(trim_view(name));
        };

        const auto extract_amount_before = [&](std::size_t anchor) -> std::optional<double> {
            if (anchor > strippedView.size())
            {
                anchor = strippedView.size();
            }
            std::size_t end = anchor;
            while (end > 0 && std::isspace(static_cast<unsigned char>(strippedView[end - 1])) != 0)
            {
                --end;
            }
            std::size_t start = end;
            while (start > 0)
            {
                const auto ch = strippedView[start - 1];
                if (!(std::isdigit(static_cast<unsigned char>(ch)) != 0 || ch == '.' || ch == ','))
                {
                    break;
                }
                --start;
            }
            if (start == end)
            {
                return std::nullopt;
            }
            return parse_number(strippedView.substr(start, end - start));
        };

        const auto extract_amount_after = [&](std::size_t anchor) -> std::optional<double> {
            std::size_t start = anchor;
            while (start < strippedView.size() && std::isspace(static_cast<unsigned char>(strippedView[start])) != 0)
            {
                ++start;
            }
            std::size_t end = start;
            while (end < strippedView.size())
            {
                const auto ch = strippedView[end];
                if (!(std::isdigit(static_cast<unsigned char>(ch)) != 0 || ch == '.' || ch == ','))
                {
                    break;
                }
                ++end;
            }
            if (start == end)
            {
                return std::nullopt;
            }
            return parse_number(strippedView.substr(start, end - start));
        };

        const std::size_t toPos = lower.find(" to ");
        const std::size_t fromPos = lower.find(" from ");
        bool playerDealt = false;
        std::string counterparty;
        std::optional<double> amountOpt;

        if (toPos != std::string::npos && (fromPos == std::string::npos || toPos < fromPos))
        {
            playerDealt = true;
            std::size_t nameStart = toPos + 4;
            std::size_t nameEnd = lower.find(" -", nameStart);
            if (nameEnd == std::string::npos)
            {
                nameEnd = strippedView.size();
            }
            counterparty = cleanup_name(strippedView.substr(nameStart, nameEnd - nameStart));
            amountOpt = extract_amount_before(toPos);
        }
        else if (fromPos != std::string::npos)
        {
            playerDealt = false;
            std::size_t nameStart = fromPos + 6;
            std::size_t nameEnd = lower.find(" -", nameStart);
            if (nameEnd == std::string::npos)
            {
                nameEnd = strippedView.size();
            }
            counterparty = cleanup_name(strippedView.substr(nameStart, nameEnd - nameStart));
            amountOpt = extract_amount_before(fromPos);
        }

        if (counterparty.empty())
        {
            if (lower.find(" hits you") != std::string::npos || lower.find(" smashes you") != std::string::npos || lower.find(" strikes you") != std::string::npos)
            {
                playerDealt = false;
                const std::array<std::string_view, 3> patterns{" hits you", " smashes you", " strikes you"};
                for (const auto& pattern : patterns)
                {
                    const auto pos = lower.find(pattern);
                    if (pos != std::string::npos)
                    {
                        counterparty = cleanup_name(strippedView.substr(0, pos));
                        if (!amountOpt.has_value())
                        {
                            amountOpt = extract_amount_before(pos);
                        }
                        break;
                    }
                }
            }
            else if (lower.find("you hit ") != std::string::npos || lower.find("your ") != std::string::npos)
            {
                playerDealt = true;
                const auto youHitPos = lower.find("you hit ");
                if (youHitPos != std::string::npos)
                {
                    const auto targetStart = youHitPos + std::string("you hit ").size();
                    auto amountPos = lower.find(" for", targetStart);
                    auto targetEnd = amountPos;
                    if (targetEnd == std::string::npos)
                    {
                        targetEnd = strippedView.size();
                    }
                    counterparty = cleanup_name(strippedView.substr(targetStart, targetEnd - targetStart));
                    if (!amountOpt.has_value() && amountPos != std::string::npos)
                    {
                        amountOpt = extract_amount_after(amountPos + 4);
                    }
                }
                else
                {
                    const auto yourPos = lower.find("your ");
                    if (yourPos != std::string::npos)
                    {
                        auto targetStart = lower.find(" hits ", yourPos);
                        if (targetStart != std::string::npos)
                        {
                            targetStart += std::string(" hits ").size();
                            auto amountPos = lower.find(" for", targetStart);
                            auto targetEnd = amountPos;
                            if (targetEnd == std::string::npos)
                            {
                                targetEnd = strippedView.size();
                            }
                            counterparty = cleanup_name(strippedView.substr(targetStart, targetEnd - targetStart));
                            if (!amountOpt.has_value() && amountPos != std::string::npos)
                            {
                                amountOpt = extract_amount_after(amountPos + 4);
                            }
                        }
                    }
                }
            }
        }

        if (counterparty.empty())
        {
            return std::nullopt;
        }

        if (!amountOpt.has_value())
        {
            const auto forPos = lower.find(" for ");
            if (forPos != std::string::npos)
            {
                amountOpt = extract_amount_after(forPos + 4);
            }
        }

        if (!amountOpt.has_value())
        {
            amountOpt = extract_amount_before(strippedView.size());
        }

        const auto amount = amountOpt.value_or(0.0);
        
        // Detect hit quality from keywords in the log line
        HitQuality quality = HitQuality::Standard;
        if (lower.find("miss") != std::string::npos)
        {
            quality = HitQuality::Miss;
        }
        else if (lower.find("glanc") != std::string::npos)  // "glances off" or "glancing"
        {
            quality = HitQuality::Glancing;
        }
        else if (lower.find("penetrat") != std::string::npos)  // "penetrates" or "penetrating"
        {
            quality = HitQuality::Penetrating;
        }
        else if (lower.find("smash") != std::string::npos)  // "smashes" or "smashing"
        {
            quality = HitQuality::Smashing;
        }
        
        // For misses, amount might be 0
        if (quality == HitQuality::Miss || amount > 0.0)
        {
            CombatDamageEvent event;
            event.playerDealt = playerDealt;
            event.amount = amount;
            event.counterparty = std::move(counterparty);
            event.quality = quality;
            event.timestamp = timestamp;
            return event;
        }
        
        return std::nullopt;
    }

    std::optional<MiningYieldEvent> parse_mining_yield_line(std::string_view line)
    {
        if (line.find("(notify)") == std::string_view::npos && line.find("(mining)") == std::string_view::npos)
        {
            return std::nullopt;
        }

        const auto strippedStorage = strip_markup(line);
        std::string_view strippedView = strippedStorage;
        auto lower = lowercase_copy(strippedView);

        constexpr std::string_view kMiningToken{"(mining)"};
        const auto miningPos = lower.find(kMiningToken);
        if (miningPos != std::string::npos)
        {
            strippedView.remove_prefix(miningPos + kMiningToken.size());
            lower = lowercase_copy(strippedView);
        }

        strippedView = trim_view(strippedView);
        lower = lowercase_copy(strippedView);

        if (lower.find(" mined ") == std::string::npos && lower.find(" mining ") == std::string::npos)
        {
            return std::nullopt;
        }

        const auto timestamp = parse_timestamp(line).value_or(std::chrono::system_clock::now());

        std::size_t numberEnd = std::string::npos;
        const std::size_t m3Pos = lower.find(" m3");
        const std::size_t unitsPos = lower.find(" units");

        if (m3Pos != std::string::npos)
        {
            numberEnd = m3Pos;
        }
        else if (unitsPos != std::string::npos)
        {
            numberEnd = unitsPos;
        }
        else
        {
            return std::nullopt;
        }

        while (numberEnd > 0 && std::isspace(static_cast<unsigned char>(strippedView[numberEnd - 1])) != 0)
        {
            --numberEnd;
        }

        std::size_t numberStart = numberEnd;
        while (numberStart > 0)
        {
            const auto ch = strippedView[numberStart - 1];
            if (!(std::isdigit(static_cast<unsigned char>(ch)) != 0 || ch == '.' || ch == ','))
            {
                break;
            }
            --numberStart;
        }

        if (numberStart == numberEnd)
        {
            return std::nullopt;
        }

        const auto volume = parse_number(strippedView.substr(numberStart, numberEnd - numberStart));
        if (volume <= 0.0)
        {
            return std::nullopt;
        }

        std::string resource;
        const auto ofPos = lower.find(" of ");
        if (ofPos != std::string::npos)
        {
            auto nameStart = ofPos + 4;
            auto nameEnd = lower.find(" worth", nameStart);
            if (nameEnd == std::string::npos)
            {
                nameEnd = lower.find('.', nameStart);
            }
            if (nameEnd == std::string::npos)
            {
                nameEnd = lower.size();
            }
            resource = std::string(trim_view(strippedView.substr(nameStart, nameEnd - nameStart)));
        }

        MiningYieldEvent event;
        event.volumeM3 = volume;
        event.resource = std::move(resource);
        event.timestamp = timestamp;
        return event;
    }
}
