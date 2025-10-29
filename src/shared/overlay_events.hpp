#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace overlay
{
    // Event types that the overlay can send to the helper
    enum class EventType : std::uint8_t
    {
        TOGGLE_FOLLOW_MODE = 1,
        TOGGLE_VISITED_SYSTEMS_TRACKING = 2,
        START_SESSION = 3,
        STOP_SESSION = 4,
        ADD_BOOKMARK = 5  // For future bookmark feature
    };

    struct OverlayEvent
    {
        EventType type;
        std::uint64_t timestamp_ms{0};
        
        // Optional payload for events that need additional data
        std::optional<std::string> text;      // For bookmark text
        std::optional<bool> for_tribe{false}; // For tribe bookmarks
        
        OverlayEvent() = default;
        explicit OverlayEvent(EventType t) : type(t) {}
    };

    // Simple ring buffer for event queue (lock-free single producer, single consumer)
    // Overlay writes, helper reads
    class EventQueue
    {
    public:
        static constexpr std::size_t MAX_EVENTS = 32;

        EventQueue() = default;

        // Try to push an event (returns false if queue is full)
        bool push(const OverlayEvent& event);

        // Try to pop an event (returns nullopt if queue is empty)
        std::optional<OverlayEvent> pop();

        // Check if queue is empty
        bool empty() const;

        // Clear all events
        void clear();

    private:
        std::uint32_t writeIndex_{0};
        std::uint32_t readIndex_{0};
        OverlayEvent events_[MAX_EVENTS];
    };
}
