#include "overlay_renderer.hpp"

#include <algorithm>
#include <chrono>
#include <thread>
#include <vector>

#include <imgui.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

namespace
{
    using namespace std::chrono_literals;

    std::uint64_t now_ms()
    {
        return static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch())
                .count());
    }

    constexpr std::uint64_t kStateStaleThresholdMs = 5000;
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
    std::uint32_t version = 0;

    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        stateCopy = currentState_;
        errorCopy = lastError_;
        updatedAtMs = lastUpdatedAtMs_;
        version = lastVersion_;
    }

    const std::uint64_t nowMsValue = now_ms();

    if (!eventWriterReady_.load())
    {
        eventWriterReady_.store(eventWriter_.ensure());
    }

    ImGui::SetNextWindowSize(ImVec2(360.0f, 0.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowBgAlpha(0.94f);

    const ImVec4 windowBg = ImVec4(0.035f, 0.035f, 0.035f, 0.96f);
    const ImVec4 titleBg = ImVec4(0.08f, 0.08f, 0.08f, 0.94f);
    const ImVec4 titleBgActive = titleBg;
    const ImVec4 accentActive = ImVec4(0.96f, 0.96f, 0.96f, 0.85f);
    const ImVec4 accentInactive = ImVec4(0.55f, 0.57f, 0.60f, 0.30f);
    const ImVec4 resizeGripIdle = ImVec4(0.80f, 0.82f, 0.85f, 0.24f);
    const ImVec4 resizeGripHot = ImVec4(0.98f, 0.98f, 0.98f, 0.88f);

    ImGui::PushStyleColor(ImGuiCol_WindowBg, windowBg);
    ImGui::PushStyleColor(ImGuiCol_TitleBg, titleBg);
    ImGui::PushStyleColor(ImGuiCol_TitleBgCollapsed, titleBg);
    ImGui::PushStyleColor(ImGuiCol_TitleBgActive, titleBgActive);
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
    drawList->AddText(ImGui::GetFont(), ImGui::GetFontSize() * ellipsisScale, dotsPos, ImGui::GetColorU32(ImVec4(0.80f, 0.80f, 0.80f, 0.9f)), "...");

    ImGui::Text("Thread ID: %lu", ::GetCurrentThreadId());
    ImGui::TextDisabled("F8 hides overlay • overlay map view disabled");
    ImGui::Separator();

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
        const double ageSeconds = updatedAtMs > 0 && nowMsValue > updatedAtMs
            ? static_cast<double>(nowMsValue - updatedAtMs) / 1000.0
            : 0.0;

        ImGui::Text("Schema version: %u", version);
        ImGui::Text("Route nodes: %zu", state.route.size());
        ImGui::Text("Generated: %.1f seconds ago", ageSeconds);
        ImGui::Text("Follow mode: %s", state.follow_mode_enabled ? "enabled" : "disabled");

        if (state.notes.has_value())
        {
            ImGui::Separator();
            ImGui::TextWrapped("Notes: %s", state.notes->c_str());
        }

        ImGui::Separator();
        ImGui::TextUnformatted("Route preview:");

        const std::size_t maxRows = 12;
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

        if (state.telemetry.has_value())
        {
            const overlay::TelemetryMetrics& telemetry = *state.telemetry;
            if (telemetry.combat.has_value() || telemetry.mining.has_value())
            {
                ImGui::Separator();
                ImGui::TextUnformatted("Telemetry");
                ImGui::Indent();

                if (telemetry.combat.has_value())
                {
                    const overlay::CombatTelemetry& combat = *telemetry.combat;
                    ImGui::Text("Combat totals: dealt %.1f | taken %.1f", combat.total_damage_dealt, combat.total_damage_taken);

                    if (combat.recent_window_seconds > 0.0)
                    {
                        const double dealtDps = combat.recent_damage_dealt / combat.recent_window_seconds;
                        const double takenDps = combat.recent_damage_taken / combat.recent_window_seconds;
                        ImGui::Text("Recent (%.0fs): dealt %.1f dmg (%.1f DPS) | taken %.1f dmg (%.1f DPS)",
                            combat.recent_window_seconds,
                            combat.recent_damage_dealt,
                            dealtDps,
                            combat.recent_damage_taken,
                            takenDps);
                    }
                    else
                    {
                        ImGui::TextDisabled("Recent combat window unavailable");
                    }

                    if (combat.last_event_ms > 0)
                    {
                        const double secondsSince = nowMsValue > combat.last_event_ms
                            ? static_cast<double>(nowMsValue - combat.last_event_ms) / 1000.0
                            : 0.0;
                        ImGui::TextDisabled("Last combat event %.1f s ago", secondsSince);
                    }
                    else
                    {
                        ImGui::TextDisabled("No combat events observed yet");
                    }
                }
                else
                {
                    ImGui::TextDisabled("Combat: no data yet");
                }

                if (telemetry.mining.has_value())
                {
                    const overlay::MiningTelemetry& mining = *telemetry.mining;
                    ImGui::Text("Mining totals: %.1f m3", mining.total_volume_m3);

                    if (mining.recent_window_seconds > 0.0)
                    {
                        const double ratePerMinute = (mining.recent_volume_m3 / mining.recent_window_seconds) * 60.0;
                        ImGui::Text("Recent (%.0fs): %.1f m3 (%.1f m3/min)",
                            mining.recent_window_seconds,
                            mining.recent_volume_m3,
                            ratePerMinute);
                    }
                    else
                    {
                        ImGui::TextDisabled("Recent mining window unavailable");
                    }

                    if (mining.last_event_ms > 0)
                    {
                        const double secondsSince = nowMsValue > mining.last_event_ms
                            ? static_cast<double>(nowMsValue - mining.last_event_ms) / 1000.0
                            : 0.0;
                        ImGui::TextDisabled("Last mining event %.1f s ago", secondsSince);
                    }
                    else
                    {
                        ImGui::TextDisabled("No mining events observed yet");
                    }

                    if (!mining.buckets.empty())
                    {
                        ImGui::Spacing();
                        ImGui::TextDisabled("Mining by resource:");
                        ImGui::Indent();
                        const std::size_t bucketLimit = std::min<std::size_t>(mining.buckets.size(), 4);
                        for (std::size_t i = 0; i < bucketLimit; ++i)
                        {
                            const overlay::TelemetryBucket& bucket = mining.buckets[i];
                            ImGui::BulletText("%s: %.1f m3 (recent %.1f m3)",
                                bucket.label.c_str(),
                                bucket.session_total,
                                bucket.recent_total);
                        }
                        if (mining.buckets.size() > bucketLimit)
                        {
                            ImGui::TextDisabled("...%zu more resources", mining.buckets.size() - bucketLimit);
                        }
                        ImGui::Unindent();
                    }
                }
                else
                {
                    ImGui::TextDisabled("Mining: no data yet");
                }

                if (telemetry.history.has_value())
                {
                    const overlay::TelemetryHistory& history = *telemetry.history;
                    ImGui::Spacing();
                    ImGui::TextDisabled("24h activity overview:");
                    ImGui::Indent();
                    ImGui::Text("Slices %zu / %u (%.0fs each)%s",
                        history.slices.size(),
                        history.capacity,
                        history.slice_seconds,
                        history.saturated ? " [rolling]" : "");

                    if (!history.slices.empty())
                    {
                        std::vector<float> combined;
                        combined.reserve(history.slices.size());
                        float maxValue = 0.0f;
                        for (const overlay::TelemetryHistorySlice& slice : history.slices)
                        {
                            const float value = static_cast<float>(slice.damage_dealt + slice.damage_taken + slice.mining_volume_m3);
                            combined.push_back(value);
                            maxValue = std::max(maxValue, value);
                        }

                        if (maxValue <= 0.0f)
                        {
                            maxValue = 1.0f;
                        }

                        ImGui::PlotLines("##telemetry_history_plot",
                            combined.data(),
                            static_cast<int>(combined.size()),
                            0,
                            nullptr,
                            0.0f,
                            maxValue * 1.1f,
                            ImVec2(0.0f, 90.0f));

                        const overlay::TelemetryHistorySlice& latest = history.slices.back();
                        ImGui::TextDisabled("Latest slice: dealt %.1f / taken %.1f dmg | mining %.1f m3",
                            latest.damage_dealt,
                            latest.damage_taken,
                            latest.mining_volume_m3);

                        const double windowMinutes = (history.slice_seconds * history.slices.size()) / 60.0;
                        ImGui::TextDisabled("Coverage: %.1f min", windowMinutes);
                    }
                    else
                    {
                        ImGui::TextDisabled("History: awaiting samples");
                    }

                    if (!history.reset_markers_ms.empty())
                    {
                        const std::uint64_t lastReset = history.reset_markers_ms.back();
                        double minutesAgo = 0.0;
                        if (nowMsValue > lastReset)
                        {
                            minutesAgo = static_cast<double>(nowMsValue - lastReset) / 60000.0;
                        }
                        ImGui::TextDisabled("Last reset %.1f min ago", minutesAgo);
                    }

                    ImGui::Unindent();
                }
                else
                {
                    ImGui::Spacing();
                    ImGui::TextDisabled("Telemetry history unavailable");
                }

                ImGui::Unindent();
            }
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

        if (state.active_route_node_id.has_value())
        {
            ImGui::Separator();
            ImGui::Text("Active route node: %s", state.active_route_node_id->c_str());
        }
    }

    if (eventWriterReady_.load())
    {
        ImGui::Separator();
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
    }
    else
    {
        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.9f, 0.2f, 0.2f, 1.0f), "Event queue unavailable.");
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
