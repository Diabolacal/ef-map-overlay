#include "overlay_renderer.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <mutex>
#include <thread>
#include <vector>

#include <imgui.h>
#include <imgui_internal.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

namespace
{
    using namespace std::chrono_literals;

    struct TelemetryResetFeedback
    {
        std::mutex mutex;
        bool inFlight{false};
        bool lastSuccess{false};
        std::uint64_t lastAttemptMs{0};
        std::uint64_t lastSuccessMs{0};
        std::string message;
    };

    TelemetryResetFeedback& telemetry_reset_feedback()
    {
        static TelemetryResetFeedback feedback;
        return feedback;
    }

    std::uint64_t now_ms()
    {
        return static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch())
                .count());
    }

    constexpr std::uint64_t kStateStaleThresholdMs = 5000;
    constexpr std::uint64_t kMiningRateHistoryWindowMs = 120000;
    constexpr std::uint64_t kMiningRateSmoothingWindowMs = 10000;

    const ImVec4 kWindowBgFocused = ImVec4(0.035f, 0.035f, 0.035f, 0.72f);
    const ImVec4 kWindowBgUnfocused = ImVec4(0.022f, 0.022f, 0.022f, 0.36f);
    const ImVec4 kTitleBgColor = ImVec4(0.080f, 0.080f, 0.080f, 0.92f);
    const ImVec4 kTabBase = ImVec4(0.520f, 0.200f, 0.030f, 0.62f);
    const ImVec4 kTabHover = ImVec4(1.000f, 0.460f, 0.020f, 0.95f);
    const ImVec4 kTabActive = ImVec4(1.000f, 0.420f, 0.000f, 0.99f);
    const ImVec4 kTabInactive = ImVec4(0.340f, 0.140f, 0.040f, 0.34f);
    const ImVec4 kButtonBase = ImVec4(0.820f, 0.350f, 0.020f, 0.80f);
    const ImVec4 kButtonHover = ImVec4(1.000f, 0.460f, 0.020f, 0.95f);
    const ImVec4 kButtonActive = ImVec4(0.820f, 0.320f, 0.015f, 0.99f);
    const ImVec4 kMiningGraphBackgroundBase = ImVec4(0.320f, 0.120f, 0.020f, 1.0f);
    const ImVec4 kMiningGraphLine = ImVec4(1.000f, 0.420f, 0.000f, 1.0f);
}

OverlayRenderer& OverlayRenderer::instance()
{
    static OverlayRenderer instance;
    return instance;
}

void OverlayRenderer::initialize(HMODULE module)
{
    if (initialized_.load())
    {
        return;
    }

    module_ = module;
    resetState();
    running_.store(true);

    pollThread_ = std::thread(&OverlayRenderer::pollLoop, this);
    initialized_.store(true);

    spdlog::info("OverlayRenderer initialized");
}

void OverlayRenderer::shutdown()
{
    if (!initialized_.load())
    {
        return;
    }

    running_.store(false);
    if (pollThread_.joinable())
    {
        pollThread_.join();
    }

    resetState();
    module_ = nullptr;
    initialized_.store(false);

    spdlog::info("OverlayRenderer shutdown complete");
}

void OverlayRenderer::resetState()
{
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        currentState_.reset();
        lastPayload_.clear();
        lastError_.clear();
        lastUpdatedAtMs_ = 0;
        lastVersion_ = 0;
        lastHeartbeatMs_ = 0;
        lastSourceOnline_ = true;
    }

    eventWriterReady_.store(false);
    autoHidden_.store(false);
    autoHideReason_.clear();
    restoreVisibleOnResume_ = false;
    currentTabIndex_ = 0;
    tabsInitialized_ = false;
    miningRateHistory_.clear();
    miningRateValues_.clear();
    combatDamageHistory_.clear();
    combatDamageValues_.clear();
    combatPeakDps_ = 1.0f;
    combatPeakDpsLastUpdateMs_ = 0;
}

void OverlayRenderer::recordMiningRateLocked(const overlay::OverlayState& state, std::uint64_t updatedAtMs)
{
    if (!state.telemetry.has_value() || !state.telemetry->mining.has_value())
    {
        return;
    }

    const overlay::MiningTelemetry& mining = *state.telemetry->mining;

    std::uint64_t timestamp = updatedAtMs != 0 ? updatedAtMs : state.generated_at_ms;
    if (timestamp == 0)
    {
        timestamp = now_ms();
    }

    bool replacedLast = false;
    if (!miningRateHistory_.empty() && miningRateHistory_.back().timestampMs == timestamp)
    {
        miningRateHistory_.back().totalVolumeM3 = mining.total_volume_m3;
        replacedLast = true;
    }
    else
    {
        miningRateHistory_.push_back({timestamp, mining.total_volume_m3});
    }

    const std::uint64_t cutoff = timestamp > kMiningRateHistoryWindowMs ? timestamp - kMiningRateHistoryWindowMs : 0;
    while (!miningRateHistory_.empty() && miningRateHistory_.front().timestampMs < cutoff)
    {
        miningRateHistory_.pop_front();
    }

    while (!miningRateValues_.empty() && miningRateValues_.front().timestampMs < cutoff)
    {
        miningRateValues_.pop_front();
    }

    float computedRate = 0.0f;

    if (miningRateHistory_.size() >= 2)
    {
        auto interpolateVolumeAt = [&](std::uint64_t targetMs) -> double {
            if (targetMs <= miningRateHistory_.front().timestampMs)
            {
                return miningRateHistory_.front().totalVolumeM3;
            }
            if (targetMs >= miningRateHistory_.back().timestampMs)
            {
                return miningRateHistory_.back().totalVolumeM3;
            }

            auto it = std::lower_bound(miningRateHistory_.begin(), miningRateHistory_.end(), targetMs,
                [](const MiningRateSample& sample, std::uint64_t value) {
                    return sample.timestampMs < value;
                });

            if (it == miningRateHistory_.begin())
            {
                return it->totalVolumeM3;
            }

            const MiningRateSample& right = *it;
            const MiningRateSample& left = *(it - 1);
            const std::uint64_t spanMs = right.timestampMs - left.timestampMs;
            if (spanMs == 0)
            {
                return right.totalVolumeM3;
            }

            const double fraction = static_cast<double>(targetMs - left.timestampMs) / static_cast<double>(spanMs);
            return static_cast<double>(left.totalVolumeM3) + fraction * static_cast<double>(right.totalVolumeM3 - left.totalVolumeM3);
        };

        const std::uint64_t anchorTimestamp = miningRateHistory_.back().timestampMs;
        const std::uint64_t earliestTimestamp = miningRateHistory_.front().timestampMs;
        const double currentVolume = interpolateVolumeAt(anchorTimestamp);

        std::uint64_t baselineTimestamp = anchorTimestamp > kMiningRateSmoothingWindowMs
            ? anchorTimestamp - kMiningRateSmoothingWindowMs
            : earliestTimestamp;
        if (baselineTimestamp < earliestTimestamp)
        {
            baselineTimestamp = earliestTimestamp;
        }

        const double baselineVolume = interpolateVolumeAt(baselineTimestamp);
        const std::uint64_t elapsedMs = anchorTimestamp > baselineTimestamp ? anchorTimestamp - baselineTimestamp : 0;

        if (elapsedMs > 0)
        {
            const double deltaVolume = currentVolume - baselineVolume;
            if (deltaVolume > 0.0)
            {
                computedRate = static_cast<float>((deltaVolume * 60000.0) / static_cast<double>(elapsedMs));
            }
        }
    }

    if (replacedLast && !miningRateValues_.empty() && miningRateValues_.back().timestampMs == timestamp)
    {
        miningRateValues_.back().rate = computedRate;
    }
    else
    {
        miningRateValues_.push_back({timestamp, computedRate});
    }
}

void OverlayRenderer::recordCombatDamageLocked(const overlay::OverlayState& state, std::uint64_t updatedAtMs)
{
    if (!state.telemetry.has_value() || !state.telemetry->combat.has_value())
    {
        return;
    }

    const overlay::CombatTelemetry& combat = *state.telemetry->combat;

    std::uint64_t timestamp = updatedAtMs != 0 ? updatedAtMs : state.generated_at_ms;
    if (timestamp == 0)
    {
        timestamp = now_ms();
    }

    bool replacedLast = false;
    if (!combatDamageHistory_.empty() && combatDamageHistory_.back().timestampMs == timestamp)
    {
        combatDamageHistory_.back().totalDamageDealt = combat.total_damage_dealt;
        combatDamageHistory_.back().totalDamageTaken = combat.total_damage_taken;
        replacedLast = true;
    }
    else
    {
        combatDamageHistory_.push_back({timestamp, combat.total_damage_dealt, combat.total_damage_taken});
    }

    // Use 120s window for combat (same as mining for consistency in visualization)
    constexpr std::uint64_t kCombatHistoryWindowMs = 120000;
    const std::uint64_t cutoff = timestamp > kCombatHistoryWindowMs ? timestamp - kCombatHistoryWindowMs : 0;
    
    while (!combatDamageHistory_.empty() && combatDamageHistory_.front().timestampMs < cutoff)
    {
        combatDamageHistory_.pop_front();
    }

    while (!combatDamageValues_.empty() && combatDamageValues_.front().timestampMs < cutoff)
    {
        combatDamageValues_.pop_front();
    }

    float computedDpsDealt = 0.0f;
    float computedDpsTaken = 0.0f;

    if (combatDamageHistory_.size() >= 2)
    {
        auto interpolateDamageAt = [&](std::uint64_t targetMs, bool dealt) -> double {
            if (targetMs <= combatDamageHistory_.front().timestampMs)
            {
                return dealt ? combatDamageHistory_.front().totalDamageDealt : combatDamageHistory_.front().totalDamageTaken;
            }
            if (targetMs >= combatDamageHistory_.back().timestampMs)
            {
                return dealt ? combatDamageHistory_.back().totalDamageDealt : combatDamageHistory_.back().totalDamageTaken;
            }

            auto it = std::lower_bound(combatDamageHistory_.begin(), combatDamageHistory_.end(), targetMs,
                [](const CombatDamageSample& sample, std::uint64_t value) {
                    return sample.timestampMs < value;
                });

            if (it == combatDamageHistory_.begin())
            {
                return dealt ? it->totalDamageDealt : it->totalDamageTaken;
            }

            const CombatDamageSample& right = *it;
            const CombatDamageSample& left = *(it - 1);
            const std::uint64_t spanMs = right.timestampMs - left.timestampMs;
            if (spanMs == 0)
            {
                return dealt ? right.totalDamageDealt : right.totalDamageTaken;
            }

            const double leftVal = dealt ? left.totalDamageDealt : left.totalDamageTaken;
            const double rightVal = dealt ? right.totalDamageDealt : right.totalDamageTaken;
            const double fraction = static_cast<double>(targetMs - left.timestampMs) / static_cast<double>(spanMs);
            return leftVal + fraction * (rightVal - leftVal);
        };

        const std::uint64_t anchorTimestamp = combatDamageHistory_.back().timestampMs;
        const std::uint64_t earliestTimestamp = combatDamageHistory_.front().timestampMs;
        
        // Use 10s window for DPS calculation (same as mining rate)
        constexpr std::uint64_t kDpsCalculationWindowMs = 10000;
        std::uint64_t baselineTimestamp = anchorTimestamp > kDpsCalculationWindowMs
            ? anchorTimestamp - kDpsCalculationWindowMs
            : earliestTimestamp;
        if (baselineTimestamp < earliestTimestamp)
        {
            baselineTimestamp = earliestTimestamp;
        }

        const double currentDealt = interpolateDamageAt(anchorTimestamp, true);
        const double currentTaken = interpolateDamageAt(anchorTimestamp, false);
        const double baselineDealt = interpolateDamageAt(baselineTimestamp, true);
        const double baselineTaken = interpolateDamageAt(baselineTimestamp, false);
        
        const std::uint64_t elapsedMs = anchorTimestamp > baselineTimestamp ? anchorTimestamp - baselineTimestamp : 0;

        if (elapsedMs > 0)
        {
            const double deltaDealt = currentDealt - baselineDealt;
            const double deltaTaken = currentTaken - baselineTaken;
            
            // Check for recent activity: if no damage change in last 2 seconds, drop DPS to zero
            // This prevents the long tail-off when combat ends
            constexpr std::uint64_t kRecentActivityWindowMs = 2000;
            const std::uint64_t recentCheckTimestamp = anchorTimestamp > kRecentActivityWindowMs 
                ? anchorTimestamp - kRecentActivityWindowMs 
                : earliestTimestamp;
            
            const double recentDealt = interpolateDamageAt(recentCheckTimestamp, true);
            const double recentTaken = interpolateDamageAt(recentCheckTimestamp, false);
            
            const bool hasRecentDealtActivity = (currentDealt - recentDealt) > 0.1;  // Threshold to avoid floating point noise
            const bool hasRecentTakenActivity = (currentTaken - recentTaken) > 0.1;
            
            if (deltaDealt > 0.0 && hasRecentDealtActivity)
            {
                // DPS = damage per second
                computedDpsDealt = static_cast<float>((deltaDealt * 1000.0) / static_cast<double>(elapsedMs));
            }
            else
            {
                computedDpsDealt = 0.0f;  // No recent activity, drop to zero immediately
            }
            
            if (deltaTaken > 0.0 && hasRecentTakenActivity)
            {
                computedDpsTaken = static_cast<float>((deltaTaken * 1000.0) / static_cast<double>(elapsedMs));
            }
            else
            {
                computedDpsTaken = 0.0f;  // No recent activity, drop to zero immediately
            }
        }
    }

    if (replacedLast && !combatDamageValues_.empty() && combatDamageValues_.back().timestampMs == timestamp)
    {
        combatDamageValues_.back().dpsDealt = computedDpsDealt;
        combatDamageValues_.back().dpsTaken = computedDpsTaken;
    }
    else
    {
        combatDamageValues_.push_back({timestamp, computedDpsDealt, computedDpsTaken});
    }
}

OverlayRenderer::TelemetryResetResult OverlayRenderer::performTelemetryReset()
{
    TelemetryResetResult result;
    result.resetMs = now_ms();

    if (!eventWriterReady_.load())
    {
        eventWriterReady_.store(eventWriter_.ensure());
    }

    if (!eventWriterReady_.load())
    {
        result.success = false;
        result.message = "Event queue unavailable";
        return result;
    }

    overlay::OverlayEvent event;
    event.type = overlay::OverlayEventType::CustomJson;
    event.timestamp_ms = result.resetMs;
    event.payload = nlohmann::json{{"action", "telemetry_reset"}}.dump();

    if (!eventWriter_.publish(event))
    {
        result.success = false;
        result.message = "Failed to publish reset event";
        return result;
    }

    result.success = true;
    result.message = "Reset requested";
    return result;
}

void OverlayRenderer::pollLoop()
{
    spdlog::info("Overlay state polling thread started");

    while (running_.load())
    {
        if (!sharedReader_.ensure())
        {
            std::this_thread::sleep_for(500ms);
            continue;
        }

        if (!eventWriterReady_.load())
        {
            eventWriterReady_.store(eventWriter_.ensure());
        }

        auto snapshot = sharedReader_.read();
        if (snapshot)
        {
            const auto payload = snapshot->json_payload;
            const auto version = snapshot->version;
            const auto updatedAt = snapshot->updated_at_ms;

            try
            {
                const auto json = nlohmann::json::parse(payload, nullptr, true, true);
                overlay::OverlayState parsedState = overlay::parse_overlay_state(json);

                {
                    std::lock_guard<std::mutex> lock(stateMutex_);
                    currentState_ = parsedState;
                    lastPayload_ = payload;
                    lastError_.clear();
                    lastUpdatedAtMs_ = updatedAt;
                    lastVersion_ = version;
                    lastHeartbeatMs_ = parsedState.heartbeat_ms;
                    lastSourceOnline_ = parsedState.source_online;
                    recordMiningRateLocked(parsedState, updatedAt);
                    recordCombatDamageLocked(parsedState, updatedAt);
                }

                if (autoHidden_.load())
                {
                    autoHidden_.store(false);
                    autoHideReason_.clear();
                    if (restoreVisibleOnResume_)
                    {
                        visible_.store(true);
                    }
                    restoreVisibleOnResume_ = false;
                }
            }
            catch (const std::exception& ex)
            {
                spdlog::error("Failed to parse overlay state from shared memory: {}", ex.what());

                {
                    std::lock_guard<std::mutex> lock(stateMutex_);
                    currentState_.reset();
                    lastPayload_ = payload;
                    lastError_ = ex.what();
                    lastUpdatedAtMs_ = updatedAt;
                    lastVersion_ = version;
                    lastHeartbeatMs_ = 0;
                    lastSourceOnline_ = false;
                }

                const bool alreadyHidden = autoHidden_.load();
                if (!alreadyHidden)
                {
                    restoreVisibleOnResume_ = visible_.load();
                    if (restoreVisibleOnResume_)
                    {
                        visible_.store(false);
                    }
                }
                autoHidden_.store(true);
                autoHideReason_ = "state parse failure";
            }
        }

        {
            const std::uint64_t currentTimeMs = now_ms();
            std::lock_guard<std::mutex> lock(stateMutex_);
            const bool haveState = currentState_.has_value();
            const std::uint64_t heartbeatCopy = lastHeartbeatMs_ == 0 ? lastUpdatedAtMs_ : lastHeartbeatMs_;
            const bool currentlyHidden = autoHidden_.load();

            if (haveState)
            {
                const bool stale = heartbeatCopy > 0 && currentTimeMs > heartbeatCopy && (currentTimeMs - heartbeatCopy) > kStateStaleThresholdMs;
                const bool offline = !lastSourceOnline_;
                const char* reason = offline ? "helper offline" : "helper heartbeat stale";

                if (offline || stale)
                {
                    if (!currentlyHidden)
                    {
                        restoreVisibleOnResume_ = visible_.load();
                        if (restoreVisibleOnResume_)
                        {
                            visible_.store(false);
                        }
                        autoHidden_.store(true);
                        autoHideReason_ = reason;
                        spdlog::info("Overlay auto-hidden (loop check): reason={}, age={}ms", autoHideReason_, heartbeatCopy == 0 || currentTimeMs <= heartbeatCopy ? 0 : (currentTimeMs - heartbeatCopy));
                    }
                    else
                    {
                        autoHideReason_ = reason;
                    }
                }
                else if (currentlyHidden && autoHideReason_ != "state parse failure")
                {
                    autoHidden_.store(false);
                    autoHideReason_.clear();
                    if (restoreVisibleOnResume_)
                    {
                        visible_.store(true);
                    }
                    restoreVisibleOnResume_ = false;
                    spdlog::info("Overlay auto-hide cleared (loop check)");
                }
            }
        }

        std::this_thread::sleep_for(200ms);
    }

    spdlog::info("Overlay state polling thread exiting");
}

void OverlayRenderer::renderImGui()
{
    if (ImGui::IsKeyPressed(ImGuiKey_F8, false))
    {
        const bool nowVisible = !visible_.load();
        visible_.store(nowVisible);
        spdlog::info("Overlay visibility toggled: {}", nowVisible ? "shown" : "hidden");
        if (eventWriterReady_.load())
        {
            overlay::OverlayEvent visibilityEvent;
            visibilityEvent.type = overlay::OverlayEventType::ToggleVisibility;
            visibilityEvent.payload = nlohmann::json{{"visible", nowVisible}}.dump();
            if (!eventWriter_.publish(visibilityEvent))
            {
                spdlog::warn("Failed to publish ToggleVisibility event");
            }
        }
    }

    if (!visible_.load() || autoHidden_.load())
    {
        return;
    }

    std::optional<overlay::OverlayState> stateCopy;
    std::string errorCopy;
    std::uint64_t updatedAtMs = 0;
    std::deque<MiningRateSample> miningRateHistoryCopy;
    std::deque<MiningRateValue> miningRateValuesCopy;

    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        stateCopy = currentState_;
        errorCopy = lastError_;
        updatedAtMs = lastUpdatedAtMs_;
        miningRateHistoryCopy = miningRateHistory_;
        miningRateValuesCopy = miningRateValues_;
    }

    const std::uint64_t nowMsValue = now_ms();
    
    // Apply exponential moving average for smooth curves (α=0.3)
    // This makes mining curves smooth while preserving actual data points
    if (miningRateValuesCopy.size() > 1)
    {
        constexpr float alpha = 0.3f;  // EMA smoothing factor (0.3 = responsive but smooth)
        float emaValue = miningRateValuesCopy[0].rate;
        
        for (size_t i = 1; i < miningRateValuesCopy.size(); ++i)
        {
            // EMA formula: EMA_new = α * value_new + (1 - α) * EMA_old
            emaValue = alpha * miningRateValuesCopy[i].rate + (1.0f - alpha) * emaValue;
            miningRateValuesCopy[i].rate = emaValue;
        }
    }
    
    // Save decay parameters for rendering interpolation
    std::uint64_t lastMiningEventMs = 0;
    std::uint64_t lastRealSampleMs = 0;
    float lastRealSampleRate = 0.0f;
    
    if (stateCopy && stateCopy->telemetry && stateCopy->telemetry->mining.has_value())
    {
        lastMiningEventMs = stateCopy->telemetry->mining->last_event_ms;
    }
    
    if (!miningRateValuesCopy.empty())
    {
        lastRealSampleMs = miningRateValuesCopy.back().timestampMs;
        lastRealSampleRate = miningRateValuesCopy.back().rate;
    }

    if (!eventWriterReady_.load())
    {
        eventWriterReady_.store(eventWriter_.ensure());
    }

    ImGui::SetNextWindowSize(ImVec2(360.0f, 0.0f), ImGuiCond_FirstUseEver);

    const ImVec4 accentActive = ImVec4(0.94f, 0.95f, 0.96f, 0.96f);
    const ImVec4 accentInactive = ImVec4(0.65f, 0.68f, 0.70f, 0.40f);
    const ImVec4 resizeGripIdle = ImVec4(0.88f, 0.90f, 0.92f, 0.36f);
    const ImVec4 resizeGripHot = ImVec4(0.95f, 0.96f, 0.98f, 0.92f);

    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_TitleBg, kTitleBgColor);
    ImGui::PushStyleColor(ImGuiCol_TitleBgCollapsed, kTitleBgColor);
    ImGui::PushStyleColor(ImGuiCol_TitleBgActive, kTitleBgColor);
    ImGui::PushStyleColor(ImGuiCol_Separator, accentInactive);
    ImGui::PushStyleColor(ImGuiCol_SeparatorHovered, accentActive);
    ImGui::PushStyleColor(ImGuiCol_SeparatorActive, accentActive);
    ImGui::PushStyleColor(ImGuiCol_ResizeGrip, resizeGripIdle);
    ImGui::PushStyleColor(ImGuiCol_ResizeGripHovered, resizeGripHot);
    ImGui::PushStyleColor(ImGuiCol_ResizeGripActive, resizeGripHot);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);

    const ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoCollapse;
    const bool windowOpen = ImGui::Begin("EF-Map Overlay", nullptr, windowFlags);
    if (!windowOpen)
    {
        ImGui::End();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(10);
        return;
    }

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const ImVec2 windowPos = ImGui::GetWindowPos();
    const ImVec2 windowSize = ImGui::GetWindowSize();
    const bool windowFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);

    const ImVec2 windowMax(windowPos.x + windowSize.x, windowPos.y + windowSize.y);
    const ImVec4 windowBgColor = windowFocused ? kWindowBgFocused : kWindowBgUnfocused;
    drawList->AddRectFilled(windowPos, windowMax, ImGui::ColorConvertFloat4ToU32(windowBgColor), 6.0f);

    drawList->PushClipRectFullScreen();
    const float accentHeight = 1.0f;
    const float accentYOffset = -0.5f;
    const ImVec2 accentMin(windowPos.x, windowPos.y + accentYOffset);
    const ImVec2 accentMax(windowPos.x + windowSize.x, windowPos.y + accentYOffset + accentHeight);
    const ImVec4 accentColor = windowFocused ? accentActive : accentInactive;
    drawList->AddRectFilled(accentMin, accentMax, ImGui::ColorConvertFloat4ToU32(accentColor));
    drawList->PopClipRect();

    const float dotsPaddingX = 18.0f;
    const float dotsPaddingY = 6.0f;
    const ImVec2 dotsPos(windowPos.x + windowSize.x - dotsPaddingX, windowPos.y + dotsPaddingY);
    const float ellipsisScale = 0.88f;

    if (!stateCopy)
    {
        ImGui::TextColored(ImVec4(0.9f, 0.6f, 0.0f, 1.0f), "Waiting for overlay state...");
        if (!errorCopy.empty())
        {
            ImGui::TextColored(ImVec4(0.9f, 0.2f, 0.2f, 1.0f), "Last error: %s", errorCopy.c_str());
        }
    }
    else
    {
        const overlay::OverlayState& state = *stateCopy;

        const overlay::TelemetryMetrics* telemetry = state.telemetry ? &*state.telemetry : nullptr;

        auto renderOverviewTab = [&]() {
            ImGui::Separator();
            ImGui::Text("Follow mode: %s", state.follow_mode_enabled ? "enabled" : "disabled");

            if (state.notes.has_value())
            {
                ImGui::Separator();
                ImGui::TextWrapped("Notes: %s", state.notes->c_str());
            }

            ImGui::Separator();
            ImGui::TextUnformatted("Route preview:");
            const std::size_t maxRows = 12;
            if (state.route.empty())
            {
                ImGui::TextDisabled("No route nodes loaded");
            }
            else
            {
                const std::size_t displayCount = std::min<std::size_t>(state.route.size(), maxRows);
                for (std::size_t i = 0; i < displayCount; ++i)
                {
                    const overlay::RouteNode& node = state.route[i];
                    ImGui::BulletText("%zu. %s (%s) -- %.2f ly %s",
                        i + 1,
                        node.display_name.c_str(),
                        node.system_id.c_str(),
                        node.distance_ly,
                        node.via_gate ? "via gate" : "jump");
                }

                if (state.route.size() > maxRows)
                {
                    ImGui::Text("...and %zu more nodes", state.route.size() - maxRows);
                }
            }

            if (state.active_route_node_id.has_value())
            {
                ImGui::Separator();
                ImGui::Text("Active route node: %s", state.active_route_node_id->c_str());
            }

            if (eventWriterReady_.load())
            {
                ImGui::Separator();
                ImGui::PushStyleColor(ImGuiCol_Button, kButtonBase);
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kButtonHover);
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, kButtonActive);

                if (ImGui::Button("Send waypoint advance event"))
                {
                    overlay::OverlayEvent event;
                    event.type = overlay::OverlayEventType::WaypointAdvanced;
                    event.payload = nlohmann::json{{"source", "overlay"}, {"sent_ms", nowMsValue}}.dump();
                    if (!eventWriter_.publish(event))
                    {
                        spdlog::warn("Failed to publish WaypointAdvanced event");
                    }
                }

                ImGui::SameLine();
                if (ImGui::Button("Request follow toggle"))
                {
                    overlay::OverlayEvent event;
                    event.type = overlay::OverlayEventType::FollowModeToggled;
                    event.payload = nlohmann::json{{"requested", true}}.dump();
                    if (!eventWriter_.publish(event))
                    {
                        spdlog::warn("Failed to publish FollowModeToggled event");
                    }
                }

                ImGui::PopStyleColor(3);
            }
            else
            {
                ImGui::Separator();
                ImGui::TextColored(ImVec4(0.9f, 0.2f, 0.2f, 1.0f), "Event queue unavailable.");
            }

            if (state.player_marker.has_value())
            {
                ImGui::Separator();
                const overlay::PlayerMarker& marker = *state.player_marker;
                ImGui::Text("Player: %s (%s)%s",
                    marker.display_name.c_str(),
                    marker.system_id.c_str(),
                    marker.is_docked ? " [Docked]" : "");
            }

            if (!state.highlighted_systems.empty())
            {
                ImGui::Separator();
                ImGui::TextUnformatted("Highlights:");
                ImGui::Indent();
                for (const overlay::HighlightedSystem& highlight : state.highlighted_systems)
                {
                    ImGui::BulletText("%s (%s) [%s]", highlight.display_name.c_str(), highlight.system_id.c_str(), highlight.category.c_str());
                    if (highlight.note.has_value())
                    {
                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.65f, 0.76f, 0.95f, 1.0f));
                        ImGui::TextWrapped("%s", highlight.note->c_str());
                        ImGui::PopStyleColor();
                    }
                }
                ImGui::Unindent();
            }

            if (state.camera_pose.has_value())
            {
                const overlay::CameraPose& pose = *state.camera_pose;
                ImGui::Separator();
                ImGui::Text("Camera position: (%.2f, %.2f, %.2f)", pose.position.x, pose.position.y, pose.position.z);
                ImGui::Text("Camera look-at: (%.2f, %.2f, %.2f)", pose.look_at.x, pose.look_at.y, pose.look_at.z);
                ImGui::Text("Camera FOV: %.1f\u00B0", pose.fov_degrees);
            }

            if (!state.hud_hints.empty())
            {
                ImGui::Separator();
                ImGui::TextUnformatted("HUD hints:");
                ImGui::Indent();
                for (const overlay::HudHint& hint : state.hud_hints)
                {
                    ImGui::BulletText("%s%s", hint.text.c_str(), hint.dismissible ? " (dismissible)" : "");
                    ImGui::SameLine();
                    ImGui::TextDisabled("[%s]", hint.active ? "active" : "inactive");
                }
                ImGui::Unindent();
            }
        };

        auto renderMiningTab = [&]() {
            // Always show the UI structure, even if no data yet
            const bool hasTelemetry = telemetry && telemetry->mining.has_value();
            
            if (!hasTelemetry)
            {
                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Text("Mining totals: 0.0 m3");
                ImGui::Spacing();
                ImGui::TextDisabled("Recent rate (m3/min)");
                
                // Show empty sparkline container
                const float sparklineHeight = 72.0f;
                const float sparklineWidth = std::max(180.0f, ImGui::GetContentRegionAvail().x);
                ImVec2 sparkPos = ImGui::GetCursorScreenPos();
                ImVec2 sparkMax = ImVec2(sparkPos.x + sparklineWidth, sparkPos.y + sparklineHeight);
                
                const float sparkAlpha = windowFocused ? kWindowBgFocused.w : kWindowBgUnfocused.w;
                ImVec4 sparkBackground = kMiningGraphBackgroundBase;
                sparkBackground.w = sparkAlpha;
                drawList->AddRectFilled(sparkPos, sparkMax, ImGui::ColorConvertFloat4ToU32(sparkBackground), 5.0f);
                
                ImGui::Dummy(ImVec2(sparklineWidth, sparklineHeight));
                ImGui::Spacing();
                ImGui::TextDisabled("Begin mining to populate telemetry data.");
                return;
            }
            
            const overlay::MiningTelemetry& mining = *telemetry->mining;

            ImGui::Separator();
            ImGui::Text("Mining totals: %.1f m3", mining.total_volume_m3);

            if (mining.recent_window_seconds > 0.0)
            {
                    ImGui::Text("Recent (%.0fs): %.1f m3",
                        mining.recent_window_seconds,
                        mining.recent_volume_m3);
            }

            if (mining.session_duration_seconds > 0.0)
            {
                const double sessionMinutes = mining.session_duration_seconds / 60.0;
                double sinceStartMinutes = sessionMinutes;
                if (mining.session_start_ms > 0 && nowMsValue > mining.session_start_ms)
                {
                    sinceStartMinutes = static_cast<double>(nowMsValue - mining.session_start_ms) / 60000.0;
                }
                ImGui::TextDisabled("Session %.1f min (started %.1f min ago)", sessionMinutes, sinceStartMinutes);
            }

            ImGui::Spacing();
            ImGui::TextDisabled("Recent rate (m3/min)");

            const float sparklineHeight = 72.0f;
            const float sparklineWidth = std::max(180.0f, ImGui::GetContentRegionAvail().x);
            const float paddingY = 6.0f;
            const float windowMsF = static_cast<float>(kMiningRateHistoryWindowMs);
                const float edgeInset = 3.0f;

            ImVec2 sparkPos = ImGui::GetCursorScreenPos();
            ImVec2 sparkMax = ImVec2(sparkPos.x + sparklineWidth, sparkPos.y + sparklineHeight);
                const float leftX = sparkPos.x + edgeInset;
                const float rightX = sparkPos.x + sparklineWidth - edgeInset;
                const float innerWidth = std::max(1.0f, rightX - leftX);
            const float innerHeight = std::max(1.0f, sparklineHeight - (paddingY * 2.0f));

            const float sparkAlpha = windowFocused ? kWindowBgFocused.w : kWindowBgUnfocused.w;
            ImVec4 sparkBackground = kMiningGraphBackgroundBase;
            sparkBackground.w = sparkAlpha;
            drawList->AddRectFilled(sparkPos, sparkMax, ImGui::ColorConvertFloat4ToU32(sparkBackground), 5.0f);
            float latestRate = 0.0f;
            float peakRate = 0.0f;

            struct RatePlotSample
            {
                float rate;
                std::uint64_t ageMs;
            };

            std::vector<RatePlotSample> plotSamples;
            std::vector<ImVec2> linePoints;
            float maxAgeMsForHover = 0.0f;

            if (!miningRateValuesCopy.empty())
            {
                std::vector<MiningRateValue> ratePoints;
                ratePoints.reserve(miningRateValuesCopy.size());
                for (const MiningRateValue& value : miningRateValuesCopy)
                {
                    ratePoints.push_back(value);
                }

                const std::uint64_t anchorTimestamp = ratePoints.back().timestampMs;
                const std::uint64_t windowStartCandidate = anchorTimestamp > kMiningRateHistoryWindowMs ? anchorTimestamp - kMiningRateHistoryWindowMs : 0;

                auto firstRelevant = std::lower_bound(ratePoints.begin(), ratePoints.end(), windowStartCandidate,
                    [](const MiningRateValue& sample, std::uint64_t value) {
                        return sample.timestampMs < value;
                    });

                std::vector<MiningRateValue> workingPoints;
                workingPoints.reserve(ratePoints.size());
                if (firstRelevant != ratePoints.begin())
                {
                    workingPoints.push_back(*(firstRelevant - 1));
                }
                for (auto it = firstRelevant; it != ratePoints.end(); ++it)
                {
                    workingPoints.push_back(*it);
                }
                if (workingPoints.empty())
                {
                    workingPoints.push_back(ratePoints.back());
                }

                const std::uint64_t earliestTimestamp = workingPoints.front().timestampMs;
                const std::uint64_t latestTimestamp = ratePoints.back().timestampMs;

                std::uint64_t displayStartTs = earliestTimestamp;
                if (latestTimestamp > kMiningRateHistoryWindowMs)
                {
                    const std::uint64_t candidateStart = latestTimestamp - kMiningRateHistoryWindowMs;
                    if (candidateStart > earliestTimestamp)
                    {
                        displayStartTs = candidateStart;
                    }
                }
                if (displayStartTs < earliestTimestamp)
                {
                    displayStartTs = earliestTimestamp;
                }

                const std::uint64_t displayCoverage = latestTimestamp > displayStartTs ? latestTimestamp - displayStartTs : 0;
                const std::uint64_t maxAgeMs = std::min<std::uint64_t>(kMiningRateHistoryWindowMs, displayCoverage);
                maxAgeMsForHover = std::min<float>(windowMsF, static_cast<float>(maxAgeMs));

                auto interpolateRateAt = [&](std::uint64_t timestamp) -> float {
                    // FIRST: Check if this timestamp is more than 10s after last MINING EVENT
                    // This ensures we return zero for ALL historical rendering after mining stops
                    if (lastMiningEventMs > 0 && timestamp > (lastMiningEventMs + 10000))
                    {
                        return 0.0f;
                    }
                    
                    if (timestamp <= workingPoints.front().timestampMs)
                    {
                        return workingPoints.front().rate;
                    }
                    
                    // Check if we're past the last real sample - apply decay ONLY if mining has stopped
                    // (detected by checking if last mining event is older than the last sample)
                    const bool miningHasStopped = lastMiningEventMs > 0 && 
                                                  lastRealSampleMs > 0 && 
                                                  lastMiningEventMs < lastRealSampleMs;
                    
                    if (miningHasStopped && timestamp > lastRealSampleMs)
                    {
                        constexpr std::uint64_t kMiningCycleMs = 7000;   // 7s hold (6s large laser cycle + 1s margin)
                        constexpr std::uint64_t kDecayWindowMs = 10000;  // Total 10s window (7s hold + 3s decay)
                        
                        const std::uint64_t timeSinceLast = timestamp - lastRealSampleMs;
                        
                        // If within one laser cycle, hold at last rate
                        if (timeSinceLast <= kMiningCycleMs)
                        {
                            return lastRealSampleRate;
                        }
                        // If in decay window, linearly decay to zero
                        else if (timeSinceLast < kDecayWindowMs)
                        {
                            const std::uint64_t decayDuration = timeSinceLast - kMiningCycleMs;
                            const std::uint64_t decayWindow = kDecayWindowMs - kMiningCycleMs;
                            const float decayFactor = 1.0f - (static_cast<float>(decayDuration) / static_cast<float>(decayWindow));
                            return lastRealSampleRate * decayFactor;
                        }
                        // Past decay window - return zero
                        else
                        {
                            return 0.0f;
                        }
                    }
                    
                    if (timestamp >= workingPoints.back().timestampMs)
                    {
                        return workingPoints.back().rate;
                    }

                    auto upper = std::lower_bound(workingPoints.begin(), workingPoints.end(), timestamp,
                        [](const MiningRateValue& sample, std::uint64_t value) {
                            return sample.timestampMs < value;
                        });

                    if (upper == workingPoints.begin())
                    {
                        return upper->rate;
                    }
                    if (upper == workingPoints.end())
                    {
                        return workingPoints.back().rate;
                    }

                    const MiningRateValue& right = *upper;
                    const MiningRateValue& left = *(upper - 1);
                    const std::uint64_t span = right.timestampMs - left.timestampMs;
                    if (span == 0)
                    {
                        return right.rate;
                    }

                    const float fraction = static_cast<float>(timestamp - left.timestampMs) / static_cast<float>(span);
                    return left.rate + fraction * (right.rate - left.rate);
                };

                // Use current time as anchor so decay logic triggers when mining stops
                const std::uint64_t anchorMs = now_ms();
                const std::uint64_t sampleIntervalMs = 250;

                // No smooth scroll offset needed - we're anchored to now
                const float smoothScrollOffsetMs = 0.0f;

                for (std::uint64_t ageMs = 0; ageMs <= maxAgeMs; ageMs += sampleIntervalMs)
                {
                    const std::uint64_t sampleTimestamp = anchorMs > ageMs ? anchorMs - ageMs : anchorMs;
                    const float rate = std::max(0.0f, interpolateRateAt(sampleTimestamp));
                    plotSamples.push_back({rate, ageMs});
                    peakRate = std::max(peakRate, rate);
                }

                if (plotSamples.empty())
                {
                    const float rateNow = std::max(0.0f, workingPoints.back().rate);
                    plotSamples.push_back({rateNow, 0});
                    peakRate = std::max(peakRate, rateNow);
                }
                else if (plotSamples.back().ageMs != maxAgeMs)
                {
                    const std::uint64_t sampleTimestamp = anchorMs > maxAgeMs ? anchorMs - maxAgeMs : anchorMs;
                    const float rate = std::max(0.0f, interpolateRateAt(sampleTimestamp));
                    plotSamples.push_back({rate, maxAgeMs});
                    peakRate = std::max(peakRate, rate);
                }

                if (peakRate <= 0.0f)
                {
                    peakRate = 1.0f;
                }

                latestRate = plotSamples.front().rate;

                linePoints.reserve(plotSamples.size());
                for (size_t i = 0; i < plotSamples.size(); ++i)
                {
                    const RatePlotSample& sample = plotSamples[i];
                    
                    // Only apply smooth scroll offset to historical data (not the head at t=0)
                    // This keeps the latest point (orange dot) pinned to the right edge
                    float effectiveAgeMs = static_cast<float>(sample.ageMs);
                    if (sample.ageMs > 0)
                    {
                        effectiveAgeMs += smoothScrollOffsetMs;
                    }
                    
                    const float normalizedTime = 1.0f - std::min(effectiveAgeMs / windowMsF, 1.0f);
                    const float x = leftX + normalizedTime * innerWidth;
                    const float normalizedRate = std::clamp(sample.rate / peakRate, 0.0f, 1.0f);
                    const float y = sparkMax.y - paddingY - normalizedRate * innerHeight;
                    linePoints.emplace_back(x, y);
                }

                if (linePoints.size() >= 2)
                {
                    drawList->AddPolyline(linePoints.data(), static_cast<int>(linePoints.size()), ImGui::ColorConvertFloat4ToU32(kMiningGraphLine), false, 2.0f);
                }
                if (!linePoints.empty())
                {
                    const ImU32 latestColor = ImGui::ColorConvertFloat4ToU32(ImVec4(1.0f, 0.52f, 0.12f, 1.0f));
                    drawList->AddCircleFilled(linePoints.front(), 3.0f, latestColor);
                }
            }
            else
            {
                const char* waitingText = "Waiting for mining rate samples...";
                const ImVec2 textSize = ImGui::CalcTextSize(waitingText);
                const ImVec2 textPos = ImVec2(
                    sparkPos.x + (sparklineWidth - textSize.x) * 0.5f,
                    sparkPos.y + (sparklineHeight - textSize.y) * 0.5f);
                drawList->AddText(textPos, ImGui::ColorConvertFloat4ToU32(ImVec4(0.95f, 0.78f, 0.56f, 0.85f)), waitingText);
            }

            ImGui::SetCursorScreenPos(sparkPos);
            ImGui::InvisibleButton("MiningRateSparkline", ImVec2(sparklineWidth, sparklineHeight));

            if (ImGui::IsItemHovered() && !plotSamples.empty())
            {
                const ImVec2 mouse = ImGui::GetIO().MousePos;
                const float relX = std::clamp((mouse.x - leftX) / innerWidth, 0.0f, 1.0f);
                const float requestedAgeMs = (1.0f - relX) * windowMsF;
                const float clampedAgeMs = std::clamp(requestedAgeMs, 0.0f, maxAgeMsForHover);

                auto it = std::lower_bound(plotSamples.begin(), plotSamples.end(), static_cast<std::uint64_t>(clampedAgeMs),
                    [](const RatePlotSample& sample, std::uint64_t ageMs) {
                        return sample.ageMs < ageMs;
                    });

                std::size_t index = 0;
                if (it == plotSamples.end())
                {
                    index = plotSamples.size() - 1;
                }
                else
                {
                    index = static_cast<std::size_t>(std::distance(plotSamples.begin(), it));
                    if (it != plotSamples.begin())
                    {
                        const std::size_t prevIndex = index - 1;
                        const std::uint64_t prevAge = plotSamples[prevIndex].ageMs;
                        const std::uint64_t currAge = plotSamples[index].ageMs;
                        const float prevDiff = std::abs(static_cast<float>(prevAge) - clampedAgeMs);
                        const float currDiff = std::abs(static_cast<float>(currAge) - clampedAgeMs);
                        if (prevDiff < currDiff)
                        {
                            index = prevIndex;
                        }
                    }
                }

                const float ageSeconds = static_cast<float>(plotSamples[index].ageMs) / 1000.0f;
                ImGui::SetTooltip("t-%.1fs: %.1f m3/min", ageSeconds, plotSamples[index].rate);
            }

            if (!plotSamples.empty())
            {
                ImGui::Text("Latest: %.1f m3/min", latestRate);
                ImGui::SameLine();
                ImGui::TextDisabled("Peak %.1f", peakRate);
                ImGui::SameLine();
                ImGui::TextDisabled("Window %.0f s", static_cast<float>(kMiningRateHistoryWindowMs) / 1000.0f);
                ImGui::SameLine();
                ImGui::TextDisabled("Smoothing %.0f s", static_cast<float>(kMiningRateSmoothingWindowMs) / 1000.0f);
            }

            if (!mining.buckets.empty())
            {
                ImGui::Spacing();
                ImGui::TextDisabled("Mining by resource (session totals):");
                ImGui::Indent();
                const std::size_t bucketLimit = std::min<std::size_t>(mining.buckets.size(), static_cast<std::size_t>(8));
                for (std::size_t i = 0; i < bucketLimit; ++i)
                {
                    const overlay::TelemetryBucket& bucket = mining.buckets[i];
                    ImGui::BulletText("%s: %.1f m3",
                        bucket.label.c_str(),
                        bucket.session_total);
                }
                if (mining.buckets.size() > bucketLimit)
                {
                    ImGui::TextDisabled("...%zu more resources", mining.buckets.size() - bucketLimit);
                }
                ImGui::Unindent();
            }

            {
                auto& feedback = telemetry_reset_feedback();
                bool resetInFlight = false;
                bool lastSuccess = false;
                std::string lastMessage;
                std::uint64_t lastSuccessMs = 0;
                {
                    std::lock_guard<std::mutex> lock(feedback.mutex);
                    resetInFlight = feedback.inFlight;
                    lastSuccess = feedback.lastSuccess;
                    lastMessage = feedback.message;
                    lastSuccessMs = feedback.lastSuccessMs;

                    // Clear message 3 seconds after successful reset
                    if (lastSuccess && lastSuccessMs > 0)
                    {
                        const std::uint64_t currentMs = now_ms();
                        if (currentMs > lastSuccessMs && (currentMs - lastSuccessMs) > 3000)
                        {
                            feedback.message.clear();
                            lastMessage.clear();
                        }
                    }
                }

                ImGui::Separator();
                const bool disableButton = resetInFlight;
                if (disableButton)
                {
                    ImGui::BeginDisabled();
                }
                ImGui::PushStyleColor(ImGuiCol_Button, kButtonBase);
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kButtonHover);
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, kButtonActive);
                const bool clicked = ImGui::Button(disableButton ? "Resetting..." : "Reset session");
                ImGui::PopStyleColor(3);
                if (disableButton)
                {
                    ImGui::EndDisabled();
                }

                if (clicked)
                {
                    bool launchReset = false;
                    {
                        std::lock_guard<std::mutex> lock(feedback.mutex);
                        if (!feedback.inFlight)
                        {
                            feedback.inFlight = true;
                            feedback.lastAttemptMs = now_ms();
                            feedback.lastSuccess = false;
                            feedback.message = "Resetting...";
                            launchReset = true;
                            resetInFlight = true;
                            lastSuccess = false;
                            lastMessage = feedback.message;
                        }
                    }

                    if (launchReset)
                    {
                        OverlayRenderer* renderer = this;
                        std::thread([renderer]() {
                            const auto result = renderer->performTelemetryReset();
                            auto& feedback = telemetry_reset_feedback();
                            std::lock_guard<std::mutex> guard(feedback.mutex);
                            feedback.inFlight = false;
                            feedback.lastSuccess = result.success;
                            feedback.message = result.message;
                            if (result.success)
                            {
                                feedback.lastSuccessMs = result.resetMs;
                            }
                        }).detach();
                    }
                }

                if (!lastMessage.empty())
                {
                    const ImVec4 color = lastSuccess ? ImVec4(0.45f, 0.86f, 0.58f, 1.0f) : ImVec4(0.9f, 0.45f, 0.45f, 1.0f);
                    ImGui::SameLine();
                    ImGui::TextColored(color, "%s", lastMessage.c_str());
                }
            }

        };

        auto renderCombatTab = [&]() {
            // Always show the UI structure, even if no data yet
            const bool hasTelemetry = telemetry && telemetry->combat.has_value();
            
            if (!hasTelemetry)
            {
                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Text("Combat totals: 0 dealt | 0 taken");
                ImGui::Spacing();
                ImGui::TextDisabled("Damage over time (2 min)");
                
                // Show empty sparkline container
                const float sparklineHeight = 144.0f;  // 2x mining height
                const float sparklineWidth = std::max(180.0f, ImGui::GetContentRegionAvail().x);
                ImVec2 sparkPos = ImGui::GetCursorScreenPos();
                ImVec2 sparkMax = ImVec2(sparkPos.x + sparklineWidth, sparkPos.y + sparklineHeight);
                
                const float sparkAlpha = windowFocused ? kWindowBgFocused.w : kWindowBgUnfocused.w;
                ImVec4 sparkBackground = kMiningGraphBackgroundBase;
                sparkBackground.w = sparkAlpha;
                drawList->AddRectFilled(sparkPos, sparkMax, ImGui::ColorConvertFloat4ToU32(sparkBackground), 5.0f);
                
                ImGui::Dummy(ImVec2(sparklineWidth, sparklineHeight));
                ImGui::Spacing();
                ImGui::TextDisabled("Engage a target to populate combat data.");
                return;
            }
            
            const overlay::CombatTelemetry& combat = *telemetry->combat;

            ImGui::Separator();
            ImGui::Text("Combat totals: %.1f dealt | %.1f taken", combat.total_damage_dealt, combat.total_damage_taken);

            // Display hit quality breakdown
            const std::uint64_t totalDealt = combat.miss_dealt + combat.glancing_dealt + combat.standard_dealt + 
                                             combat.penetrating_dealt + combat.smashing_dealt;
            const std::uint64_t totalTaken = combat.miss_taken + combat.glancing_taken + combat.standard_taken + 
                                             combat.penetrating_taken + combat.smashing_taken;
            
            if (totalDealt > 0)
            {
                ImGui::Text("Hits dealt: %llu (%llu pen, %llu smash, %llu std, %llu glance) | %llu miss",
                    totalDealt, combat.penetrating_dealt, combat.smashing_dealt, 
                    combat.standard_dealt, combat.glancing_dealt, combat.miss_dealt);
            }
            
            if (totalTaken > 0)
            {
                ImGui::Text("Hits taken: %llu (%llu pen, %llu smash, %llu std, %llu glance) | %llu miss",
                    totalTaken, combat.penetrating_taken, combat.smashing_taken, 
                    combat.standard_taken, combat.glancing_taken, combat.miss_taken);
            }

            if (combat.session_duration_seconds > 0.0)
            {
                const double sessionMinutes = combat.session_duration_seconds / 60.0;
                const double sinceStartMinutes = combat.session_duration_seconds / 60.0;
                if (combat.session_start_ms > 0)
                {
                    ImGui::TextDisabled("Session %.1f min (started %.1f min ago)", sessionMinutes, sinceStartMinutes);
                }
            }

            ImGui::Spacing();
            ImGui::TextDisabled("Damage over time (2 min)");

            // Dual-line sparkline: orange for dealt, red for taken
            std::deque<CombatDamageValue> combatDamageValuesCopy;
            {
                std::lock_guard<std::mutex> lock(stateMutex_);
                combatDamageValuesCopy = combatDamageValues_;
            }

            const float sparklineHeight = 144.0f;  // 2x mining height for better combat visibility
            const float sparklineWidth = std::max(180.0f, ImGui::GetContentRegionAvail().x);
            ImVec2 sparkPos = ImGui::GetCursorScreenPos();
            ImVec2 sparkMax = ImVec2(sparkPos.x + sparklineWidth, sparkPos.y + sparklineHeight);

            const float sparkAlpha = windowFocused ? kWindowBgFocused.w : kWindowBgUnfocused.w;
            ImVec4 sparkBackground = kMiningGraphBackgroundBase;
            sparkBackground.w = sparkAlpha;
            drawList->AddRectFilled(sparkPos, sparkMax, ImGui::ColorConvertFloat4ToU32(sparkBackground), 5.0f);

            if (!combatDamageValuesCopy.empty())
            {
                constexpr float paddingX = 8.0f;
                constexpr float paddingY = 6.0f;
                const float leftX = sparkPos.x + paddingX;
                const float rightX = sparkMax.x - paddingX;
                const float innerWidth = std::max(1.0f, rightX - leftX);
                const float innerHeight = std::max(1.0f, sparklineHeight - (paddingY * 2.0f));

                // Use current time as anchor (like mining sparkline) for smooth continuous scrolling
                const std::uint64_t anchorMs = now_ms();
                constexpr std::uint64_t windowMs = 120000;  // 2 minutes
                constexpr std::uint64_t sampleIntervalMs = 250;  // Sample every 250ms for smooth curves
                
                // Helper to interpolate DPS at any timestamp
                auto interpolateDpsAt = [&](std::uint64_t targetMs) -> std::pair<float, float> {
                    if (combatDamageValuesCopy.empty())
                    {
                        return {0.0f, 0.0f};
                    }
                    
                    // Before first sample
                    if (targetMs <= combatDamageValuesCopy.front().timestampMs)
                    {
                        return {combatDamageValuesCopy.front().dpsDealt, combatDamageValuesCopy.front().dpsTaken};
                    }
                    
                    // After last sample
                    if (targetMs >= combatDamageValuesCopy.back().timestampMs)
                    {
                        return {combatDamageValuesCopy.back().dpsDealt, combatDamageValuesCopy.back().dpsTaken};
                    }
                    
                    // Find bounding samples
                    auto upper = std::lower_bound(combatDamageValuesCopy.begin(), combatDamageValuesCopy.end(), targetMs,
                        [](const CombatDamageValue& sample, std::uint64_t value) {
                            return sample.timestampMs < value;
                        });
                    
                    if (upper == combatDamageValuesCopy.begin())
                    {
                        return {upper->dpsDealt, upper->dpsTaken};
                    }
                    
                    const CombatDamageValue& right = *upper;
                    const CombatDamageValue& left = *(upper - 1);
                    const std::uint64_t span = right.timestampMs - left.timestampMs;
                    if (span == 0)
                    {
                        return {right.dpsDealt, right.dpsTaken};
                    }
                    
                    const float fraction = static_cast<float>(targetMs - left.timestampMs) / static_cast<float>(span);
                    const float dealtDps = left.dpsDealt + fraction * (right.dpsDealt - left.dpsDealt);
                    const float takenDps = left.dpsTaken + fraction * (right.dpsTaken - left.dpsTaken);
                    return {dealtDps, takenDps};
                };

                // Plot actual data points directly without interpolation to avoid oscillation
                // The raw data points are stable, and ImGui's AddPolyline will handle smooth rendering
                float observedPeakDps = 1.0f;
                for (const auto& value : combatDamageValuesCopy)
                {
                    observedPeakDps = std::max(observedPeakDps, std::max(value.dpsDealt, value.dpsTaken));
                }
                
                // Stable peak tracking with slow decay to prevent bouncing
                // Quantize peak to prevent sub-pixel oscillation from tiny floating-point changes
                constexpr float kPeakQuantum = 1.0f;  // Round to nearest 1.0 DPS
                {
                    std::lock_guard<std::mutex> lock(stateMutex_);
                    
                    // If we see a new peak, update immediately and quantize
                    if (observedPeakDps > combatPeakDps_)
                    {
                        combatPeakDps_ = std::ceil(observedPeakDps / kPeakQuantum) * kPeakQuantum;
                        combatPeakDpsLastUpdateMs_ = anchorMs;
                    }
                    // Otherwise, allow slow decay: 1% per second, but only update if change exceeds quantum
                    else if (combatPeakDpsLastUpdateMs_ > 0)
                    {
                        const std::uint64_t elapsedMs = anchorMs > combatPeakDpsLastUpdateMs_ 
                            ? anchorMs - combatPeakDpsLastUpdateMs_ 
                            : 0;
                        
                        // Only decay every 100ms to reduce jitter
                        if (elapsedMs >= 100)
                        {
                            const float elapsedSeconds = static_cast<float>(elapsedMs) / 1000.0f;
                            const float decayFactor = std::pow(0.99f, elapsedSeconds);  // 1% decay per second
                            
                            const float decayedPeak = combatPeakDps_ * decayFactor;
                            
                            // Don't let it decay below observed peak
                            float newPeak = std::max(decayedPeak, observedPeakDps);
                            
                            // Quantize to prevent oscillation
                            newPeak = std::ceil(newPeak / kPeakQuantum) * kPeakQuantum;
                            
                            // Only update if the change is significant
                            if (std::abs(newPeak - combatPeakDps_) >= kPeakQuantum)
                            {
                                combatPeakDps_ = newPeak;
                                combatPeakDpsLastUpdateMs_ = anchorMs;
                            }
                            
                            // Prevent it from going too low
                            if (combatPeakDps_ < 1.0f)
                            {
                                combatPeakDps_ = 1.0f;
                            }
                        }
                    }
                    else
                    {
                        combatPeakDps_ = std::ceil(observedPeakDps / kPeakQuantum) * kPeakQuantum;
                        combatPeakDpsLastUpdateMs_ = anchorMs;
                    }
                }
                
                const float peakDps = combatPeakDps_;

                // Build line points from actual data (no interpolation to avoid oscillation)
                std::vector<ImVec2> linePointsDealt;
                std::vector<ImVec2> linePointsTaken;
                linePointsDealt.reserve(combatDamageValuesCopy.size());
                linePointsTaken.reserve(combatDamageValuesCopy.size());
                
                const float windowMsF = static_cast<float>(windowMs);
                
                // Plot raw data points directly - they're stable and won't oscillate
                for (const auto& value : combatDamageValuesCopy)
                {
                    // Calculate age from anchor (now)
                    const std::uint64_t ageMs = anchorMs > value.timestampMs ? anchorMs - value.timestampMs : 0;
                    
                    // Skip points outside the window
                    if (ageMs > windowMs)
                    {
                        continue;
                    }
                    
                    // normalizedTime: 0.0 = left edge (old), 1.0 = right edge (now)
                    const float normalizedTime = 1.0f - std::min(static_cast<float>(ageMs) / windowMsF, 1.0f);
                    const float x = leftX + normalizedTime * innerWidth;
                    
                    // Plot dealt damage (orange line)
                    {
                        const float normalizedDps = std::clamp(value.dpsDealt / peakDps, 0.0f, 1.0f);
                        const float y = sparkMax.y - paddingY - normalizedDps * innerHeight;
                        linePointsDealt.push_back(ImVec2(x, y));
                    }
                    
                    // Plot taken damage (red line)
                    {
                        const float normalizedDps = std::clamp(value.dpsTaken / peakDps, 0.0f, 1.0f);
                        const float y = sparkMax.y - paddingY - normalizedDps * innerHeight;
                        linePointsTaken.push_back(ImVec2(x, y));
                    }
                }

                // Draw taken damage line (red) first so dealt (orange) draws on top
                if (linePointsTaken.size() >= 2)
                {
                    const ImVec4 takenColor = ImVec4(1.0f, 0.2f, 0.1f, sparkAlpha);  // Red for incoming damage
                    const ImU32 takenColorU32 = ImGui::ColorConvertFloat4ToU32(takenColor);
                    drawList->AddPolyline(linePointsTaken.data(), static_cast<int>(linePointsTaken.size()), takenColorU32, false, 2.0f);
                    
                    // Draw dot at latest point (first element = ageMs=0 = now = right edge)
                    if (!linePointsTaken.empty())
                    {
                        drawList->AddCircleFilled(linePointsTaken.front(), 3.0f, takenColorU32);
                    }
                }

                // Draw dealt damage line (orange) on top
                if (linePointsDealt.size() >= 2)
                {
                    ImVec4 dealtColor = kMiningGraphLine;  // Orange for outgoing damage
                    dealtColor.w = sparkAlpha;
                    const ImU32 dealtColorU32 = ImGui::ColorConvertFloat4ToU32(dealtColor);
                    drawList->AddPolyline(linePointsDealt.data(), static_cast<int>(linePointsDealt.size()), dealtColorU32, false, 2.0f);
                    
                    // Draw dot at latest point (first element = ageMs=0 = now = right edge)
                    if (!linePointsDealt.empty())
                    {
                        drawList->AddCircleFilled(linePointsDealt.front(), 3.0f, dealtColorU32);
                    }
                }

                // Invisible button for hover detection + tooltips
                ImGui::SetCursorScreenPos(sparkPos);
                ImGui::InvisibleButton("CombatDamageSparkline", ImVec2(sparklineWidth, sparklineHeight));

                if (ImGui::IsItemHovered() && !combatDamageValuesCopy.empty())
                {
                    const ImVec2 mouse = ImGui::GetIO().MousePos;
                    const float relX = std::clamp((mouse.x - leftX) / innerWidth, 0.0f, 1.0f);
                    // Convert mouse X to timestamp: right edge (relX=1.0) is now, left edge (relX=0.0) is old
                    const float ageMs = (1.0f - relX) * windowMsF;
                    const std::uint64_t requestedTimestamp = anchorMs > static_cast<std::uint64_t>(ageMs) 
                        ? anchorMs - static_cast<std::uint64_t>(ageMs) 
                        : anchorMs;

                    // Find closest data point to the mouse position
                    auto it = std::lower_bound(combatDamageValuesCopy.begin(), combatDamageValuesCopy.end(), 
                        requestedTimestamp,
                        [](const CombatDamageValue& value, std::uint64_t timestamp) {
                            return value.timestampMs < timestamp;
                        });

                    std::size_t index = 0;
                    if (it == combatDamageValuesCopy.end())
                    {
                        index = combatDamageValuesCopy.size() - 1;
                    }
                    else
                    {
                        index = static_cast<std::size_t>(std::distance(combatDamageValuesCopy.begin(), it));
                        if (it != combatDamageValuesCopy.begin())
                        {
                            const std::size_t prevIndex = index - 1;
                            const std::uint64_t prevTs = combatDamageValuesCopy[prevIndex].timestampMs;
                            const std::uint64_t currTs = combatDamageValuesCopy[index].timestampMs;
                            const std::uint64_t prevDiff = requestedTimestamp > prevTs ? requestedTimestamp - prevTs : prevTs - requestedTimestamp;
                            const std::uint64_t currDiff = currTs > requestedTimestamp ? currTs - requestedTimestamp : requestedTimestamp - currTs;
                            if (prevDiff < currDiff)
                            {
                                index = prevIndex;
                            }
                        }
                    }

                    const CombatDamageValue& hoveredValue = combatDamageValuesCopy[index];
                    const std::uint64_t hoveredAgeMs = anchorMs > hoveredValue.timestampMs ? anchorMs - hoveredValue.timestampMs : 0;
                    const float ageSeconds = static_cast<float>(hoveredAgeMs) / 1000.0f;
                    
                    ImGui::SetTooltip("t-%.1fs: %.1f dealt | %.1f taken DPS", 
                        ageSeconds, hoveredValue.dpsDealt, hoveredValue.dpsTaken);
                }

                // Show current DPS values if any (most recent = last in deque)
                if (!combatDamageValuesCopy.empty())
                {
                    const float latestDealt = combatDamageValuesCopy.back().dpsDealt;
                    const float latestTaken = combatDamageValuesCopy.back().dpsTaken;
                    ImGui::Text("Current: %.1f DPS dealt | %.1f DPS taken", latestDealt, latestTaken);
                    ImGui::TextDisabled("Peak: %.1f DPS", peakDps);
                }
            }
            else
            {
                ImGui::Dummy(ImVec2(sparklineWidth, sparklineHeight));
                ImGui::TextDisabled("No combat data yet");
            }

            // Reset button (matching mining tab style)
            {
                auto& feedback = telemetry_reset_feedback();
                bool resetInFlight = false;
                bool lastSuccess = false;
                std::string lastMessage;
                std::uint64_t lastSuccessMs = 0;
                {
                    std::lock_guard<std::mutex> lock(feedback.mutex);
                    resetInFlight = feedback.inFlight;
                    lastSuccess = feedback.lastSuccess;
                    lastMessage = feedback.message;
                    lastSuccessMs = feedback.lastSuccessMs;

                    // Clear message 3 seconds after successful reset
                    if (lastSuccess && lastSuccessMs > 0)
                    {
                        const std::uint64_t currentMs = now_ms();
                        if (currentMs > lastSuccessMs && (currentMs - lastSuccessMs) > 3000)
                        {
                            feedback.message.clear();
                            lastMessage.clear();
                        }
                    }
                }

                ImGui::Separator();
                if (resetInFlight)
                {
                    ImGui::TextDisabled("Resetting session...");
                }
                else
                {
                    ImGui::PushStyleColor(ImGuiCol_Button, kButtonBase);
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kButtonHover);
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive, kButtonActive);

                    if (ImGui::Button("Reset Session"))
                    {
                        std::thread([this]() {
                            auto& fb = telemetry_reset_feedback();
                            {
                                std::lock_guard<std::mutex> lock(fb.mutex);
                                fb.inFlight = true;
                                fb.message.clear();
                            }

                            auto result = performTelemetryReset();

                            {
                                std::lock_guard<std::mutex> lock(fb.mutex);
                                fb.inFlight = false;
                                fb.lastSuccess = result.success;
                                fb.lastAttemptMs = result.resetMs;
                                fb.message = result.message;
                                if (result.success)
                                {
                                    fb.lastSuccessMs = result.resetMs;
                                }
                            }
                        }).detach();
                    }

                    ImGui::PopStyleColor(3);

                    if (!lastMessage.empty())
                    {
                        ImGui::SameLine();
                        if (lastSuccess)
                        {
                            ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "%s", lastMessage.c_str());
                        }
                        else
                        {
                            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", lastMessage.c_str());
                        }
                    }
                }
            }
        };

        constexpr int kTabOverview = 0;
        constexpr int kTabMining = 1;
        constexpr int kTabCombat = 2;

        if (currentTabIndex_ < kTabOverview || currentTabIndex_ > kTabCombat)
        {
            currentTabIndex_ = kTabOverview;
        }

    ImGui::PushStyleColor(ImGuiCol_Tab, kTabBase);
    ImGui::PushStyleColor(ImGuiCol_TabHovered, kTabHover);
    ImGui::PushStyleColor(ImGuiCol_TabActive, kTabActive);
    ImGui::PushStyleColor(ImGuiCol_TabUnfocused, kTabInactive);
    ImGui::PushStyleColor(ImGuiCol_TabUnfocusedActive, kTabActive);

        if (ImGui::BeginTabBar("EFOverlayTabs"))
        {
            auto beginTab = [&](const char* label, int index, auto&& renderFn) {
                ImGuiTabItemFlags flags = 0;
                if (!tabsInitialized_ && index == currentTabIndex_)
                {
                    flags |= ImGuiTabItemFlags_SetSelected;
                }

                if (ImGui::BeginTabItem(label, nullptr, flags))
                {
                    currentTabIndex_ = index;
                    tabsInitialized_ = true;
                    renderFn();
                    ImGui::EndTabItem();
                }
            };

            beginTab("Overview", kTabOverview, renderOverviewTab);
            beginTab("Mining", kTabMining, renderMiningTab);
            beginTab("Combat", kTabCombat, renderCombatTab);

            ImGui::EndTabBar();
        }

    ImGui::PopStyleColor(5);

    const ImVec4 ellipsisColor = ImVec4(0.92f, 0.93f, 0.95f, 0.96f);
    drawList->AddText(ImGui::GetFont(), ImGui::GetFontSize() * ellipsisScale, dotsPos, ImGui::GetColorU32(ellipsisColor), "...");
    }

    ImGui::End();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(10);
}

std::optional<overlay::OverlayState> OverlayRenderer::latestState(std::uint32_t& version, std::uint64_t& updatedAtMs, std::string& error) const
{
    std::lock_guard<std::mutex> lock(stateMutex_);
    version = lastVersion_;
    updatedAtMs = lastUpdatedAtMs_;
    error = lastError_;
    if (currentState_)
    {
        return currentState_;
    }
    return std::nullopt;
}
