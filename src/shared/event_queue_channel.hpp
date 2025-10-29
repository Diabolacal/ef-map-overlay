#pragma once

#include "overlay_events.hpp"
#include <Windows.h>
#include <memory>
#include <optional>

namespace overlay
{
    // Writer side (overlay uses this to send events to helper)
    class EventQueueWriter
    {
    public:
        EventQueueWriter();
        ~EventQueueWriter();

        bool initialize();
        bool isInitialized() const { return initialized_; }
        
        // Post an event (thread-safe, non-blocking)
        bool postEvent(const OverlayEvent& event);

    private:
        bool initialized_{false};
        HANDLE fileMapping_{nullptr};
        EventQueue* queue_{nullptr};
    };

    // Reader side (helper uses this to receive events from overlay)
    class EventQueueReader
    {
    public:
        EventQueueReader();
        ~EventQueueReader();

        bool initialize();
        bool isInitialized() const { return initialized_; }
        
        // Get next event (returns nullopt if queue is empty)
        std::optional<OverlayEvent> getNextEvent();
        
        // Clear all pending events
        void clearAll();

    private:
        bool initialized_{false};
        HANDLE fileMapping_{nullptr};
        EventQueue* queue_{nullptr};
    };
}
