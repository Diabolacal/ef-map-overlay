#pragma once

#include <atomic>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace overlay
{
    constexpr int event_schema_version = 1;
    constexpr const wchar_t* event_shared_memory_name = L"Local\\EFOverlayEventQueue";
    constexpr std::size_t event_queue_slots = 64;
    constexpr std::size_t event_payload_capacity = 512;

    enum class OverlayEventType : std::uint16_t
    {
        None = 0,
        ToggleVisibility = 1,
        FollowModeToggled = 2,
        WaypointAdvanced = 3,
        HudHintDismissed = 4,
        VisitedSystemsTrackingToggled = 5,
        SessionStartRequested = 6,
        SessionStopRequested = 7,
        BookmarkCreateRequested = 8,
        PscanTriggerRequested = 9,
        CustomJson = 1000
    };

    struct OverlayEvent
    {
        OverlayEventType type{OverlayEventType::None};
        std::uint64_t timestamp_ms{0};
        std::string payload;
    };

    struct EventDequeueResult
    {
        std::vector<OverlayEvent> events;
        std::uint32_t dropped{0};
    };

    class OverlayEventWriter
    {
    public:
        OverlayEventWriter();
        ~OverlayEventWriter();

        OverlayEventWriter(const OverlayEventWriter&) = delete;
        OverlayEventWriter& operator=(const OverlayEventWriter&) = delete;

        bool ensure();
        bool publish(const OverlayEvent& event);

    private:
        void* mappingHandle_{nullptr};
        void* view_{nullptr};
        std::size_t capacity_{0};
    };

    class OverlayEventReader
    {
    public:
        OverlayEventReader();
        ~OverlayEventReader();

        OverlayEventReader(const OverlayEventReader&) = delete;
        OverlayEventReader& operator=(const OverlayEventReader&) = delete;

        bool ensure();
        std::optional<OverlayEvent> poll_once();
        EventDequeueResult drain();

    private:
        void* mappingHandle_{nullptr};
        void* view_{nullptr};
        std::size_t capacity_{0};
        std::uint32_t lastDropped_{0};
    };

    OverlayEvent parse_event(const std::string& payload, OverlayEventType type, std::uint64_t timestamp_ms);
    std::string serialize_event_payload(const OverlayEvent& event);
}
