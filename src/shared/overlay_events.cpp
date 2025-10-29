#include "overlay_events.hpp"
#include <atomic>
#include <cstring>

namespace overlay
{
    bool EventQueue::push(const OverlayEvent& event)
    {
        const std::uint32_t currentWrite = writeIndex_;
        const std::uint32_t nextWrite = (currentWrite + 1) % MAX_EVENTS;
        
        // Check if queue is full
        if (nextWrite == readIndex_)
        {
            return false;
        }

        events_[currentWrite] = event;
        writeIndex_ = nextWrite;
        return true;
    }

    std::optional<OverlayEvent> EventQueue::pop()
    {
        const std::uint32_t currentRead = readIndex_;
        
        // Check if queue is empty
        if (currentRead == writeIndex_)
        {
            return std::nullopt;
        }

        OverlayEvent event = events_[currentRead];
        readIndex_ = (currentRead + 1) % MAX_EVENTS;
        return event;
    }

    bool EventQueue::empty() const
    {
        return readIndex_ == writeIndex_;
    }

    void EventQueue::clear()
    {
        readIndex_ = 0;
        writeIndex_ = 0;
    }
}
