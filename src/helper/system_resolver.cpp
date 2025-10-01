#include "system_resolver.hpp"

#include "system_resolver_data.hpp"

#include <algorithm>
#include <cctype>

#include <spdlog/spdlog.h>

namespace helper::logs
{
    namespace
    {
        bool is_ascii_space(char ch)
        {
            return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n' || ch == '\f' || ch == '\v';
        }
    }

    SystemResolver::SystemResolver()
    {
        entries_.reserve(kSystemEntries.size());
        for (const auto& entry : kSystemEntries)
        {
            const auto key = normalize(entry.name);
            if (key.empty())
            {
                continue;
            }

            auto& slot = entries_[key];
            if (slot.id.empty())
            {
                slot.id = entry.id;
            }
            else if (slot.id != entry.id)
            {
                if (!slot.ambiguous)
                {
                    ambiguous_.push_back(entry.name);
                }
                slot.ambiguous = true;
            }
        }

        if (!ambiguous_.empty())
        {
            spdlog::warn("SystemResolver encountered {} duplicate system names", ambiguous_.size());
        }
    }

    std::optional<std::string> SystemResolver::resolve(std::string_view name) const
    {
        const auto key = normalize(name);
        if (key.empty())
        {
            return std::nullopt;
        }

        const auto it = entries_.find(key);
        if (it == entries_.end() || it->second.ambiguous)
        {
            return std::nullopt;
        }

        return it->second.id;
    }

    std::string SystemResolver::normalize(std::string_view name)
    {
        std::string output;
        output.reserve(name.size());
        bool lastWasSpace = false;

        auto trim = [](std::string_view value) {
            std::size_t start = 0;
            while (start < value.size() && is_ascii_space(value[start]))
            {
                ++start;
            }
            if (start == value.size())
            {
                return std::string_view{};
            }
            std::size_t end = value.size();
            while (end > start && is_ascii_space(value[end - 1]))
            {
                --end;
            }
            return value.substr(start, end - start);
        };

        const auto trimmed = trim(name);
        for (char ch : trimmed)
        {
            if (is_ascii_space(ch))
            {
                if (!output.empty() && !lastWasSpace)
                {
                    output.push_back(' ');
                    lastWasSpace = true;
                }
            }
            else
            {
                output.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
                lastWasSpace = false;
            }
        }

        if (!output.empty() && output.back() == ' ')
        {
            output.pop_back();
        }

        return output;
    }
}
