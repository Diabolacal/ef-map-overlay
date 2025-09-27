#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace overlay
{
    constexpr const wchar_t* shared_memory_name = L"Local\\EFOverlaySharedState";
    constexpr std::size_t shared_memory_capacity = 64 * 1024; // 64 KiB

    struct SharedMemorySnapshot
    {
        std::uint32_t version = 0;
        std::uint64_t updated_at_ms = 0;
        std::string json_payload;
    };

    class SharedMemoryWriter
    {
    public:
        SharedMemoryWriter();
        ~SharedMemoryWriter();

        SharedMemoryWriter(const SharedMemoryWriter&) = delete;
        SharedMemoryWriter& operator=(const SharedMemoryWriter&) = delete;

        bool ensure();
        bool write(const std::string& payload, std::uint32_t schemaVersion, std::uint64_t updatedAtMs);

    private:
        void* mappingHandle_ = nullptr;
        void* view_ = nullptr;
        std::size_t capacity_ = 0;
    };

    class SharedMemoryReader
    {
    public:
        SharedMemoryReader();
        ~SharedMemoryReader();

        SharedMemoryReader(const SharedMemoryReader&) = delete;
        SharedMemoryReader& operator=(const SharedMemoryReader&) = delete;

        bool ensure();
        std::optional<SharedMemorySnapshot> read();

    private:
        void* mappingHandle_ = nullptr;
        void* view_ = nullptr;
        std::size_t capacity_ = 0;
        std::uint32_t lastVersion_ = 0;
    };
}
