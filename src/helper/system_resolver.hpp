#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace helper::logs
{
    class SystemResolver
    {
    public:
        SystemResolver();

        SystemResolver(const SystemResolver&) = delete;
        SystemResolver& operator=(const SystemResolver&) = delete;

        std::optional<std::string> resolve(std::string_view name) const;
        const std::vector<std::string>& ambiguousNames() const noexcept { return ambiguous_; }

    private:
        struct Entry
        {
            std::string id;
            bool ambiguous{false};
        };

        static std::string normalize(std::string_view name);

        std::unordered_map<std::string, Entry> entries_;
        std::vector<std::string> ambiguous_;
    };
}
