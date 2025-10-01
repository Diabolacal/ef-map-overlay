#include "event_channel.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

namespace overlay
{
    namespace
    {
        constexpr std::uint32_t event_header_magic = 0x45464551; // 'EFEQ'

        struct EventHeader
        {
            std::uint32_t magic;
            std::uint32_t schema_version;
            std::uint32_t slot_count;
            std::uint32_t slot_payload_size;
            std::atomic<std::uint32_t> write_index;
            std::atomic<std::uint32_t> read_index;
            std::atomic<std::uint32_t> dropped_events;
            std::uint32_t reserved;
        };

        struct EventSlot
        {
            std::atomic<std::uint16_t> type;
            std::uint16_t flags;
            std::uint32_t payload_size;
            std::uint64_t timestamp_ms;
            std::array<char, event_payload_capacity> payload;
        };

        constexpr std::size_t header_size = sizeof(EventHeader);
        constexpr std::size_t slot_size = sizeof(EventSlot);
        constexpr std::size_t mapping_size = header_size + (event_queue_slots * slot_size);

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

        EventHeader* header_from_view(void* view)
        {
            return static_cast<EventHeader*>(view);
        }

        EventSlot* slots_from_view(void* view)
        {
            auto* base = static_cast<std::byte*>(view);
            return reinterpret_cast<EventSlot*>(base + header_size);
        }

        std::uint64_t monotonic_millis()
        {
            using namespace std::chrono;
            return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
        }
    }

    OverlayEventWriter::OverlayEventWriter() = default;

    OverlayEventWriter::~OverlayEventWriter()
    {
        close_mapping(mappingHandle_, view_);
    }

    bool OverlayEventWriter::ensure()
    {
        if (view_)
        {
            return true;
        }

        mappingHandle_ = ::CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, static_cast<DWORD>(mapping_size), event_shared_memory_name);
        if (!mappingHandle_)
        {
            spdlog::error("Failed to create overlay event queue mapping (error {})", ::GetLastError());
            return false;
        }

        view_ = ::MapViewOfFile(mappingHandle_, FILE_MAP_WRITE | FILE_MAP_READ, 0, 0, mapping_size);
        if (!view_)
        {
            spdlog::error("Failed to map overlay event queue (error {})", ::GetLastError());
            close_mapping(mappingHandle_, view_);
            return false;
        }

        capacity_ = mapping_size;

        auto* header = header_from_view(view_);
        if (header->magic != event_header_magic)
        {
            header->magic = event_header_magic;
            header->schema_version = static_cast<std::uint32_t>(event_schema_version);
            header->slot_count = static_cast<std::uint32_t>(event_queue_slots);
            header->slot_payload_size = static_cast<std::uint32_t>(event_payload_capacity);
            header->write_index.store(0, std::memory_order_relaxed);
            header->read_index.store(0, std::memory_order_relaxed);
            header->dropped_events.store(0, std::memory_order_relaxed);
            header->reserved = 0;

            auto* slots = slots_from_view(view_);
            for (std::size_t i = 0; i < event_queue_slots; ++i)
            {
                slots[i].type.store(static_cast<std::uint16_t>(OverlayEventType::None), std::memory_order_relaxed);
                slots[i].flags = 0;
                slots[i].payload_size = 0;
                slots[i].timestamp_ms = 0;
                slots[i].payload.fill('\0');
            }
        }

        return true;
    }

    bool OverlayEventWriter::publish(const OverlayEvent& event)
    {
        if (!ensure())
        {
            return false;
        }

        auto* header = header_from_view(view_);
        auto* slots = slots_from_view(view_);

        const std::uint32_t slotCount = header->slot_count;
        auto writeIndex = header->write_index.load(std::memory_order_acquire);
        auto readIndex = header->read_index.load(std::memory_order_acquire);
        auto nextIndex = (writeIndex + 1) % slotCount;

        if (nextIndex == readIndex)
        {
            // queue full; drop the oldest event
            readIndex = (readIndex + 1) % slotCount;
            header->read_index.store(readIndex, std::memory_order_release);
            header->dropped_events.fetch_add(1, std::memory_order_acq_rel);
        }

        auto& slot = slots[writeIndex];
        slot.type.store(static_cast<std::uint16_t>(event.type), std::memory_order_relaxed);
        slot.flags = 0;
        slot.timestamp_ms = event.timestamp_ms == 0 ? monotonic_millis() : event.timestamp_ms;

        const auto payloadBytes = std::min<std::size_t>(event.payload.size(), event_payload_capacity);
        slot.payload_size = static_cast<std::uint32_t>(payloadBytes);
        if (payloadBytes > 0)
        {
            std::memcpy(slot.payload.data(), event.payload.data(), payloadBytes);
        }
        if (payloadBytes < event_payload_capacity)
        {
            slot.payload[payloadBytes] = '\0';
        }

        header->write_index.store(nextIndex, std::memory_order_release);
        return true;
    }

    OverlayEventReader::OverlayEventReader() = default;

    OverlayEventReader::~OverlayEventReader()
    {
        close_mapping(mappingHandle_, view_);
    }

    bool OverlayEventReader::ensure()
    {
        if (view_)
        {
            return true;
        }

        mappingHandle_ = ::OpenFileMappingW(FILE_MAP_READ | FILE_MAP_WRITE, FALSE, event_shared_memory_name);
        if (!mappingHandle_)
        {
            return false;
        }

        view_ = ::MapViewOfFile(mappingHandle_, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, mapping_size);
        if (!view_)
        {
            spdlog::warn("Failed to map overlay event queue for reading (error {})", ::GetLastError());
            close_mapping(mappingHandle_, view_);
            return false;
        }

        capacity_ = mapping_size;
        return true;
    }

    std::optional<OverlayEvent> OverlayEventReader::poll_once()
    {
        if (!ensure())
        {
            return std::nullopt;
        }

        auto* header = header_from_view(view_);
        auto* slots = slots_from_view(view_);

        const auto writeIndex = header->write_index.load(std::memory_order_acquire);
        auto readIndex = header->read_index.load(std::memory_order_acquire);
        if (readIndex == writeIndex)
        {
            lastDropped_ = header->dropped_events.load(std::memory_order_acquire);
            return std::nullopt;
        }

        auto& slot = slots[readIndex];
        OverlayEvent event;
        event.type = static_cast<OverlayEventType>(slot.type.load(std::memory_order_relaxed));
        event.timestamp_ms = slot.timestamp_ms;
        event.payload.assign(slot.payload.data(), slot.payload.data() + slot.payload_size);

        auto nextIndex = (readIndex + 1) % header->slot_count;
        header->read_index.store(nextIndex, std::memory_order_release);
        lastDropped_ = header->dropped_events.load(std::memory_order_acquire);
        return event;
    }

    EventDequeueResult OverlayEventReader::drain()
    {
        EventDequeueResult result;
        while (true)
        {
            auto event = poll_once();
            if (!event)
            {
                break;
            }
            result.events.push_back(std::move(*event));
        }
        result.dropped = lastDropped_;
        return result;
    }

    OverlayEvent parse_event(const std::string& payload, OverlayEventType type, std::uint64_t timestamp_ms)
    {
        OverlayEvent event;
        event.type = type;
        event.timestamp_ms = timestamp_ms;
        event.payload = payload;
        return event;
    }

    std::string serialize_event_payload(const OverlayEvent& event)
    {
        nlohmann::json json;
        json["type"] = static_cast<std::uint32_t>(event.type);
        json["timestamp_ms"] = event.timestamp_ms;
        json["payload"] = event.payload;
        json["schema_version"] = event_schema_version;
        return json.dump();
    }
}
