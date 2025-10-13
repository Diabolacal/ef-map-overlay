#include "star_catalog.hpp"

#include <cctype>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <string_view>

namespace overlay
{
    namespace
    {
        constexpr std::string_view kMagic{"EFSTARS1"};
        constexpr std::size_t kHeaderSize = 44;
        constexpr std::size_t kPackedRecordSize = 36;

        template <typename T>
        [[nodiscard]] T read_le(const std::uint8_t* data)
        {
            T value{};
            std::memcpy(&value, data, sizeof(T));
            return value;
        }

        [[nodiscard]] const std::uint8_t* require_bytes(const std::uint8_t* cursor, const std::uint8_t* end, std::size_t count)
        {
            if (static_cast<std::size_t>(end - cursor) < count)
            {
                throw std::runtime_error("Star catalog truncated");
            }
            return cursor;
        }

        bool is_ascii_space(char ch)
        {
            switch (ch)
            {
            case ' ': case '\t': case '\r': case '\n': case '\f': case '\v':
                return true;
            default:
                return false;
            }
        }

        std::string normalize_name(std::string_view name)
        {
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
            if (trimmed.empty())
            {
                return {};
            }

            std::string output;
            output.reserve(trimmed.size());
            bool lastWasSpace = false;

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

    const StarCatalogRecord* StarCatalog::find_by_system_id(std::uint32_t system_id) const
    {
        const auto it = index_by_system_id_.find(system_id);
        if (it == index_by_system_id_.end())
        {
            return nullptr;
        }

        const auto index = it->second;
        if (index >= records.size())
        {
            return nullptr;
        }

        return &records[index];
    }

    const StarCatalogRecord* StarCatalog::find_by_name(std::string_view name) const
    {
        const auto normalized = normalize_name(name);
        if (normalized.empty())
        {
            return nullptr;
        }

        const auto it = index_by_name_.find(normalized);
        if (it == index_by_name_.end())
        {
            return nullptr;
        }

        const auto index = it->second;
        if (index >= records.size())
        {
            return nullptr;
        }

        return &records[index];
    }

    std::string_view StarCatalog::name_for(const StarCatalogRecord& record) const
    {
        if (record.name_offset + record.name_length > name_blob_.size())
        {
            return std::string_view{};
        }

        return std::string_view{name_blob_.data() + record.name_offset, record.name_length};
    }

    StarCatalog load_star_catalog(std::span<const std::uint8_t> data)
    {
        if (data.size() < kHeaderSize)
        {
            throw std::runtime_error("Star catalog too small");
        }

        const auto* cursor = data.data();
        const auto* end = cursor + data.size();

        const auto* magic_bytes = require_bytes(cursor, end, kMagic.size());
        cursor += kMagic.size();
        if (std::string_view{reinterpret_cast<const char*>(magic_bytes), kMagic.size()} != kMagic)
        {
            throw std::runtime_error("Star catalog magic mismatch");
        }

        const auto* version_bytes = require_bytes(cursor, end, sizeof(std::uint16_t));
        const auto version = read_le<std::uint16_t>(version_bytes);
        cursor += sizeof(std::uint16_t);

        const auto* record_size_bytes = require_bytes(cursor, end, sizeof(std::uint16_t));
        const auto record_size = read_le<std::uint16_t>(record_size_bytes);
        cursor += sizeof(std::uint16_t);

        const auto* star_count_bytes = require_bytes(cursor, end, sizeof(std::uint32_t));
        const auto star_count = read_le<std::uint32_t>(star_count_bytes);
        cursor += sizeof(std::uint32_t);

        if (record_size < kPackedRecordSize)
        {
            throw std::runtime_error("Star catalog record size unsupported");
        }

        StarCatalog catalog;
        catalog.version = version;
        catalog.record_size = record_size;

        const auto* bbox_min_bytes = require_bytes(cursor, end, 3 * sizeof(float));
        catalog.bbox_min.x = read_le<float>(bbox_min_bytes + 0);
        catalog.bbox_min.y = read_le<float>(bbox_min_bytes + sizeof(float));
        catalog.bbox_min.z = read_le<float>(bbox_min_bytes + 2 * sizeof(float));
        cursor += 3 * sizeof(float);

        const auto* bbox_max_bytes = require_bytes(cursor, end, 3 * sizeof(float));
        catalog.bbox_max.x = read_le<float>(bbox_max_bytes + 0);
        catalog.bbox_max.y = read_le<float>(bbox_max_bytes + sizeof(float));
        catalog.bbox_max.z = read_le<float>(bbox_max_bytes + 2 * sizeof(float));
        cursor += 3 * sizeof(float);

        const auto* strings_size_bytes = require_bytes(cursor, end, sizeof(std::uint32_t));
        const auto strings_size = read_le<std::uint32_t>(strings_size_bytes);
        cursor += sizeof(std::uint32_t);

        const auto records_bytes = static_cast<std::size_t>(record_size) * static_cast<std::size_t>(star_count);
        const auto* records_ptr = require_bytes(cursor, end, records_bytes);
        cursor += records_bytes;

        const auto* strings_ptr = require_bytes(cursor, end, strings_size);
        cursor += strings_size;

        if (cursor != end)
        {
            throw std::runtime_error("Star catalog contains trailing bytes");
        }

        catalog.records.reserve(star_count);
        catalog.name_blob_.assign(reinterpret_cast<const char*>(strings_ptr), strings_size);
        catalog.index_by_system_id_.reserve(star_count);
        catalog.index_by_name_.reserve(star_count);

        for (std::uint32_t i = 0; i < star_count; ++i)
        {
            const auto* record_ptr = records_ptr + static_cast<std::size_t>(i) * record_size;
            StarCatalogRecord record{};
            record.system_id = read_le<std::uint32_t>(record_ptr + 0);
            record.region_id = read_le<std::uint32_t>(record_ptr + 4);
            record.constellation_id = read_le<std::uint32_t>(record_ptr + 8);
            record.name_offset = read_le<std::uint32_t>(record_ptr + 12);
            record.name_length = read_le<std::uint16_t>(record_ptr + 16);
            record.spectral_id = *(record_ptr + 18);
            record.flags = *(record_ptr + 19);
            record.position.x = read_le<float>(record_ptr + 20);
            record.position.y = read_le<float>(record_ptr + 24);
            record.position.z = read_le<float>(record_ptr + 28);
            record.security = read_le<float>(record_ptr + 32);

            if (record.name_offset + record.name_length > catalog.name_blob_.size())
            {
                throw std::runtime_error("Star catalog name out of range");
            }

            const auto index = catalog.records.size();
            catalog.index_by_system_id_[record.system_id] = index;
            catalog.records.push_back(record);

            const std::string_view name_view{catalog.name_blob_.data() + record.name_offset, record.name_length};
            const std::string normalized = normalize_name(name_view);
            if (!normalized.empty() && catalog.index_by_name_.find(normalized) == catalog.index_by_name_.end())
            {
                catalog.index_by_name_.emplace(normalized, index);
            }
        }

        return catalog;
    }

    StarCatalog load_star_catalog_from_file(const std::filesystem::path& path)
    {
        std::ifstream stream(path, std::ios::binary);
        if (!stream)
        {
            throw std::runtime_error("Failed to open star catalog file: " + path.string());
        }

        stream.seekg(0, std::ios::end);
        const auto size = static_cast<std::streamoff>(stream.tellg());
        if (size < 0)
        {
            throw std::runtime_error("Failed to determine star catalog file size: " + path.string());
        }
        stream.seekg(0, std::ios::beg);

        std::vector<std::uint8_t> buffer(static_cast<std::size_t>(size));
        if (!stream.read(reinterpret_cast<char*>(buffer.data()), size))
        {
            throw std::runtime_error("Failed to read star catalog file: " + path.string());
        }

        return load_star_catalog(buffer);
    }
}
