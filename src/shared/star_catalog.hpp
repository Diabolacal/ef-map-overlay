#pragma once

#include "overlay_schema.hpp"

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace overlay
{
    struct StarCatalogRecord
    {
        std::uint32_t system_id{0};
        std::uint32_t region_id{0};
        std::uint32_t constellation_id{0};
        std::uint32_t name_offset{0};
        std::uint16_t name_length{0};
        std::uint8_t spectral_id{0};
        std::uint8_t flags{0};
        Vec3f position{};
        float security{0.0f};
    };

    class StarCatalog
    {
    public:
        std::uint16_t version{0};
        std::uint16_t record_size{0};
        Vec3f bbox_min{};
        Vec3f bbox_max{};
        std::vector<StarCatalogRecord> records;

        [[nodiscard]] std::size_t size() const noexcept { return records.size(); }
        [[nodiscard]] bool empty() const noexcept { return records.empty(); }

        [[nodiscard]] const StarCatalogRecord* find_by_system_id(std::uint32_t system_id) const;
        [[nodiscard]] const StarCatalogRecord* find_by_name(std::string_view name) const;
        [[nodiscard]] std::string_view name_for(const StarCatalogRecord& record) const;

    private:
        friend StarCatalog load_star_catalog(std::span<const std::uint8_t> data);
        friend StarCatalog load_star_catalog_from_file(const std::filesystem::path& path);

        std::string name_blob_;
        std::unordered_map<std::uint32_t, std::size_t> index_by_system_id_;
        std::unordered_map<std::string, std::size_t> index_by_name_;
    };

    [[nodiscard]] StarCatalog load_star_catalog(std::span<const std::uint8_t> data);
    [[nodiscard]] StarCatalog load_star_catalog_from_file(const std::filesystem::path& path);
}
