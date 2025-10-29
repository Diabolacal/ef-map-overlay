#include "event_queue_channel.hpp"
#include <spdlog/spdlog.h>

namespace overlay
{
    static constexpr const wchar_t* kEventQueueName = L"Local\\EFMapOverlayEvents";
    static constexpr std::size_t kEventQueueSize = sizeof(EventQueue);

    // Writer implementation
    EventQueueWriter::EventQueueWriter() = default;

    EventQueueWriter::~EventQueueWriter()
    {
        if (queue_)
        {
            UnmapViewOfFile(queue_);
            queue_ = nullptr;
        }
        if (fileMapping_)
        {
            CloseHandle(fileMapping_);
            fileMapping_ = nullptr;
        }
    }

    bool EventQueueWriter::initialize()
    {
        if (initialized_)
        {
            return true;
        }

        // Try to open existing mapping first (helper creates it)
        fileMapping_ = OpenFileMappingW(FILE_MAP_WRITE, FALSE, kEventQueueName);
        
        if (!fileMapping_)
        {
            // Helper hasn't created it yet, create it ourselves
            fileMapping_ = CreateFileMappingW(
                INVALID_HANDLE_VALUE,
                nullptr,
                PAGE_READWRITE,
                0,
                static_cast<DWORD>(kEventQueueSize),
                kEventQueueName);

            if (!fileMapping_)
            {
                spdlog::error("EventQueueWriter: Failed to create file mapping (error {})", GetLastError());
                return false;
            }
        }

        queue_ = static_cast<EventQueue*>(MapViewOfFile(fileMapping_, FILE_MAP_WRITE, 0, 0, kEventQueueSize));
        if (!queue_)
        {
            spdlog::error("EventQueueWriter: Failed to map view of file (error {})", GetLastError());
            CloseHandle(fileMapping_);
            fileMapping_ = nullptr;
            return false;
        }

        initialized_ = true;
        spdlog::info("EventQueueWriter: Initialized successfully");
        return true;
    }

    bool EventQueueWriter::postEvent(const OverlayEvent& event)
    {
        if (!initialized_ || !queue_)
        {
            return false;
        }

        const bool success = queue_->push(event);
        if (!success)
        {
            spdlog::warn("EventQueueWriter: Queue full, event dropped (type {})", static_cast<int>(event.type));
        }
        return success;
    }

    // Reader implementation
    EventQueueReader::EventQueueReader() = default;

    EventQueueReader::~EventQueueReader()
    {
        if (queue_)
        {
            UnmapViewOfFile(queue_);
            queue_ = nullptr;
        }
        if (fileMapping_)
        {
            CloseHandle(fileMapping_);
            fileMapping_ = nullptr;
        }
    }

    bool EventQueueReader::initialize()
    {
        if (initialized_)
        {
            return true;
        }

        // Helper creates the mapping
        fileMapping_ = CreateFileMappingW(
            INVALID_HANDLE_VALUE,
            nullptr,
            PAGE_READWRITE,
            0,
            static_cast<DWORD>(kEventQueueSize),
            kEventQueueName);

        if (!fileMapping_)
        {
            spdlog::error("EventQueueReader: Failed to create file mapping (error {})", GetLastError());
            return false;
        }

        queue_ = static_cast<EventQueue*>(MapViewOfFile(fileMapping_, FILE_MAP_ALL_ACCESS, 0, 0, kEventQueueSize));
        if (!queue_)
        {
            spdlog::error("EventQueueReader: Failed to map view of file (error {})", GetLastError());
            CloseHandle(fileMapping_);
            fileMapping_ = nullptr;
            return false;
        }

        // Initialize the queue structure
        queue_->clear();

        initialized_ = true;
        spdlog::info("EventQueueReader: Initialized successfully");
        return true;
    }

    std::optional<OverlayEvent> EventQueueReader::getNextEvent()
    {
        if (!initialized_ || !queue_)
        {
            return std::nullopt;
        }

        return queue_->pop();
    }

    void EventQueueReader::clearAll()
    {
        if (initialized_ && queue_)
        {
            queue_->clear();
        }
    }
}
