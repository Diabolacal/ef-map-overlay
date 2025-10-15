#pragma once

#include <atomic>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <deque>

#include <windows.h>

#include "overlay_schema.hpp"
#include "shared_memory_channel.hpp"
#include "event_channel.hpp"

class OverlayRenderer {
public:
    static OverlayRenderer& instance();

    void initialize(HMODULE module);
    void shutdown();

    bool isInitialized() const noexcept { return initialized_.load(); }
    bool isVisible() const noexcept { return visible_.load(); }
    void setVisible(bool visible) noexcept { visible_.store(visible); }
    void renderImGui();
    std::optional<overlay::OverlayState> latestState(std::uint32_t& version, std::uint64_t& updatedAtMs, std::string& error) const;

private:
    OverlayRenderer() = default;
    ~OverlayRenderer() = default;

    OverlayRenderer(const OverlayRenderer&) = delete;
    OverlayRenderer& operator=(const OverlayRenderer&) = delete;

    void pollLoop();
    void resetState();
    void recordMiningRateLocked(const overlay::OverlayState& state, std::uint64_t updatedAtMs);

    struct TelemetryResetResult
    {
        bool success{false};
        std::uint64_t resetMs{0};
        std::string message;
    };

    TelemetryResetResult performTelemetryReset();

    std::atomic_bool initialized_{false};
    std::atomic_bool running_{false};
    std::atomic_bool visible_{true};
    std::atomic_bool autoHidden_{false};
    std::thread pollThread_;
    HMODULE module_{nullptr};

    overlay::SharedMemoryReader sharedReader_;
    overlay::OverlayEventWriter eventWriter_;
    std::atomic_bool eventWriterReady_{false};
    mutable std::mutex stateMutex_;
    std::string lastPayload_;
    std::optional<overlay::OverlayState> currentState_;
    std::string lastError_;
    std::uint64_t lastUpdatedAtMs_{0};
    std::uint32_t lastVersion_{0};
    std::uint64_t lastHeartbeatMs_{0};
    bool lastSourceOnline_{true};
    std::string autoHideReason_;
    bool restoreVisibleOnResume_{false};
    int currentTabIndex_{0};
    bool tabsInitialized_{false};

    struct MiningRateSample
    {
        std::uint64_t timestampMs{0};
        double totalVolumeM3{0.0};
    };
    std::deque<MiningRateSample> miningRateHistory_;

    struct MiningRateValue
    {
        std::uint64_t timestampMs{0};
        float rate{0.0f};
    };
    std::deque<MiningRateValue> miningRateValues_;
};
