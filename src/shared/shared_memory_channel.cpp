#include "shared_memory_channel.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>

#include <spdlog/spdlog.h>

#include <array>
#include <cstddef>
#include <cstring>

namespace overlay
{
    namespace
    {
        constexpr std::uint32_t header_magic = 0x45464F53; // 'EFOS'
        struct SharedHeader
        {
            std::uint32_t magic;
            std::uint32_t schema_version;
            std::uint32_t payload_size;
            std::uint32_t reserved;
            std::uint64_t updated_at_ms;
        };

        bool validate_header(const SharedHeader& header)
        {
            return header.magic == header_magic && header.payload_size <= shared_memory_capacity - sizeof(SharedHeader);
        }

        void close_mapping(void*& mappingHandle, void*& view)
        {
            if (view)
            {
                ::UnmapViewOfFile(view);
                view = nullptr;
            }

            if (mappingHandle)
            {
                ::CloseHandle(mappingHandle);
                mappingHandle = nullptr;
            }
        }
    }

    SharedMemoryWriter::SharedMemoryWriter() = default;

    SharedMemoryWriter::~SharedMemoryWriter()
    {
        close_mapping(mappingHandle_, view_);
    }

    bool SharedMemoryWriter::ensure()
    {
        if (view_)
        {
            return true;
        }

        mappingHandle_ = ::CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, static_cast<DWORD>(shared_memory_capacity), shared_memory_name);
        if (!mappingHandle_)
        {
            spdlog::error("Failed to create shared memory mapping (error {})", ::GetLastError());
            return false;
        }

        view_ = ::MapViewOfFile(mappingHandle_, FILE_MAP_WRITE | FILE_MAP_READ, 0, 0, shared_memory_capacity);
        if (!view_)
        {
            spdlog::error("Failed to map view of shared memory (error {})", ::GetLastError());
            close_mapping(mappingHandle_, view_);
            return false;
        }

        capacity_ = shared_memory_capacity;
        return true;
    }

    bool SharedMemoryWriter::write(const std::string& payload, std::uint32_t schemaVersion, std::uint64_t updatedAtMs)
    {
        if (!ensure())
        {
            return false;
        }

        if (payload.size() > capacity_ - sizeof(SharedHeader))
        {
            spdlog::warn("Shared payload truncated ({} bytes > capacity {})", payload.size(), capacity_ - sizeof(SharedHeader));
            return false;
        }

        auto* base = static_cast<std::byte*>(view_);
        auto* headerPtr = reinterpret_cast<SharedHeader*>(base);
        auto* data = reinterpret_cast<std::byte*>(headerPtr + 1);

        SharedHeader header{};
        header.magic = header_magic;
        header.schema_version = schemaVersion;
        header.payload_size = static_cast<std::uint32_t>(payload.size());
        header.reserved = 0;
        header.updated_at_ms = updatedAtMs;

        std::memcpy(data, payload.data(), payload.size());
        std::memcpy(headerPtr, &header, sizeof(SharedHeader));

        return true;
    }

    SharedMemoryReader::SharedMemoryReader() = default;

    SharedMemoryReader::~SharedMemoryReader()
    {
        close_mapping(mappingHandle_, view_);
    }

    bool SharedMemoryReader::ensure()
    {
        if (view_)
        {
            return true;
        }

        mappingHandle_ = ::OpenFileMappingW(FILE_MAP_READ, FALSE, shared_memory_name);
        if (!mappingHandle_)
        {
            return false;
        }

        view_ = ::MapViewOfFile(mappingHandle_, FILE_MAP_READ, 0, 0, shared_memory_capacity);
        if (!view_)
        {
            spdlog::warn("Failed to map view of shared memory for reading (error {})", ::GetLastError());
            close_mapping(mappingHandle_, view_);
            return false;
        }

        capacity_ = shared_memory_capacity;
        return true;
    }

    std::optional<SharedMemorySnapshot> SharedMemoryReader::read()
    {
        if (!ensure())
        {
            return std::nullopt;
        }

        const auto* header = static_cast<const SharedHeader*>(view_);
        if (!validate_header(*header))
        {
            return std::nullopt;
        }

        if (header->payload_size == 0)
        {
            return std::nullopt;
        }

        const auto* data = reinterpret_cast<const char*>(header + 1);
        SharedMemorySnapshot snapshot;
        snapshot.version = header->schema_version;
        snapshot.updated_at_ms = header->updated_at_ms;
        snapshot.json_payload.assign(data, header->payload_size);
        lastVersion_ = header->schema_version;
        return snapshot;
    }
}
