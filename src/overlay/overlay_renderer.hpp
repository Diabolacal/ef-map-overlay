#pragma once

#include <atomic>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

#include <windows.h>

#include "overlay_schema.hpp"
#include "shared_memory_channel.hpp"

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

    std::atomic_bool initialized_{false};
    std::atomic_bool running_{false};
    std::atomic_bool visible_{true};
    std::thread pollThread_;
    HMODULE module_{nullptr};

    overlay::SharedMemoryReader sharedReader_;
    mutable std::mutex stateMutex_;
    std::string lastPayload_;
    std::optional<overlay::OverlayState> currentState_;
    std::string lastError_;
    std::uint64_t lastUpdatedAtMs_{0};
    std::uint32_t lastVersion_{0};
};
