#include "overlay_renderer.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <imgui.h>
#include <nlohmann/json.hpp>
#include <spdlog/sinks/msvc_sink.h>
#include <spdlog/spdlog.h>

namespace
{
    void ensure_logger()
    {
        if (!spdlog::default_logger())
        {
            auto sink = std::make_shared<spdlog::sinks::msvc_sink_mt>();
            auto logger = std::make_shared<spdlog::logger>("ef-overlay-module", sink);
            logger->set_level(spdlog::level::debug);
            spdlog::set_default_logger(logger);
        }

        spdlog::set_pattern("[%H:%M:%S.%e] [%^%l%$] %v");
        spdlog::flush_on(spdlog::level::info);
    }

    std::uint64_t now_ms()
    {
        const auto now = std::chrono::system_clock::now();
        return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count());
    }
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
        spdlog::debug("OverlayRenderer already initialized");
        return;
    }

    ensure_logger();

    ::OutputDebugStringA("[ef-overlay] OverlayRenderer::initialize called\n");

    module_ = module;
    resetState();

    running_.store(true);
    pollThread_ = std::thread(&OverlayRenderer::pollLoop, this);
    eventWriterReady_.store(eventWriter_.ensure());
    if (!eventWriterReady_.load())
    {
        spdlog::warn("Overlay event writer initialization failed; events will be suppressed");
    }
    initialized_.store(true);

    const auto thread_tag = std::hash<std::thread::id>{}(pollThread_.get_id());
    spdlog::info("Overlay renderer initialized (module={}, poller=0x{:X})", static_cast<void*>(module_), static_cast<unsigned long long>(thread_tag));
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

    initialized_.store(false);
    module_ = nullptr;

    ::OutputDebugStringA("[ef-overlay] OverlayRenderer::shutdown completed\n");
    spdlog::info("Overlay renderer shut down");
}

void OverlayRenderer::resetState()
{
    std::lock_guard<std::mutex> lock(stateMutex_);
    lastPayload_.clear();
    currentState_.reset();
    lastError_.clear();
    lastUpdatedAtMs_ = 0;
    lastVersion_ = 0;
    lastHeartbeatMs_ = 0;
    lastSourceOnline_ = true;
    autoHidden_.store(false);
    autoHideReason_.clear();
    restoreVisibleOnResume_ = false;
    eventWriterReady_.store(false);
}

void OverlayRenderer::pollLoop()
{
    using namespace std::chrono_literals;

    spdlog::info("Overlay state polling thread started");

    while (running_.load())
    {
        const auto snapshot = sharedReader_.read();
        if (snapshot)
        {
            const auto currentTimeMs = now_ms();
            constexpr std::uint64_t staleThresholdMs = 5000;
            std::lock_guard<std::mutex> lock(stateMutex_);
            if (snapshot->json_payload != lastPayload_)
            {
                try
                {
                    const auto json = nlohmann::json::parse(snapshot->json_payload, nullptr, true, true);
                    currentState_ = overlay::parse_overlay_state(json);
                    lastPayload_ = snapshot->json_payload;
                    lastError_.clear();
                    lastUpdatedAtMs_ = snapshot->updated_at_ms;
                    lastVersion_ = snapshot->version;
                    lastHeartbeatMs_ = currentState_->heartbeat_ms == 0 ? snapshot->updated_at_ms : currentState_->heartbeat_ms;
                    lastSourceOnline_ = currentState_->source_online;
                    auto effectiveHeartbeat = lastHeartbeatMs_;
                    if (effectiveHeartbeat == 0)
                    {
                        effectiveHeartbeat = snapshot->updated_at_ms;
                    }

                    const bool offline = !lastSourceOnline_;
                    const bool stale = effectiveHeartbeat > 0 && currentTimeMs > effectiveHeartbeat && (currentTimeMs - effectiveHeartbeat) > staleThresholdMs;
                    const char* reason = offline ? "helper offline" : "helper heartbeat stale";

                    const bool currentlyHidden = autoHidden_.load();
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
                            spdlog::info("Overlay auto-hidden: {} (offline={}, stale={}, age={}ms)", autoHideReason_, offline, stale, effectiveHeartbeat == 0 ? 0 : (currentTimeMs - effectiveHeartbeat));
                        }
                        else
                        {
                            autoHideReason_ = reason;
                        }
                    }
                    else if (currentlyHidden)
                    {
                        autoHidden_.store(false);
                        autoHideReason_.clear();
                        if (restoreVisibleOnResume_)
                        {
                            visible_.store(true);
                        }
                        restoreVisibleOnResume_ = false;
                        spdlog::info("Overlay auto-hide cleared: helper heartbeat restored");
                    }
                    spdlog::debug("Overlay state updated from shared memory (version={}, updated_at={})", lastVersion_, lastUpdatedAtMs_);
                }
                catch (const std::exception& ex)
                {
                    currentState_.reset();
                    lastError_ = ex.what();
                    lastPayload_ = snapshot->json_payload;
                    lastUpdatedAtMs_ = snapshot->updated_at_ms;
                    lastVersion_ = snapshot->version;
                    restoreVisibleOnResume_ = visible_.load();
                    if (restoreVisibleOnResume_)
                    {
                        visible_.store(false);
                    }
                    autoHidden_.store(true);
                    autoHideReason_ = "state parse failure";
                    spdlog::error("Failed to parse overlay state from shared memory: {}", ex.what());
                }
            }
        }

        {
            const auto currentTimeMs = now_ms();
            std::lock_guard<std::mutex> lock(stateMutex_);
            const std::uint64_t heartbeatCopy = lastHeartbeatMs_ == 0 ? lastUpdatedAtMs_ : lastHeartbeatMs_;
            const bool haveState = currentState_.has_value();
            if (haveState)
            {
                constexpr std::uint64_t staleThresholdMs = 5000;
                const bool stale = heartbeatCopy > 0 && currentTimeMs > heartbeatCopy && (currentTimeMs - heartbeatCopy) > staleThresholdMs;
                const bool offline = !lastSourceOnline_;
                const char* reason = offline ? "helper offline" : "helper heartbeat stale";
                const bool currentlyHidden = autoHidden_.load();

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
                        spdlog::info("Overlay auto-hidden (loop check): reason={}, age={}ms", autoHideReason_, heartbeatCopy == 0 ? 0 : (currentTimeMs - heartbeatCopy));
                    }
                    else
                    {
                        autoHideReason_ = reason;
                    }
                }
                else if (currentlyHidden)
                {
                    if (autoHideReason_ != "state parse failure")
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

    if (!visible_.load())
    {
        return;
    }

    if (autoHidden_.load())
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

    const auto nowMs = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());

    ImGui::SetNextWindowSize(ImVec2(360.0f, 0.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowBgAlpha(0.92f);

    ImGui::Begin("EF-Map Overlay", nullptr, ImGuiWindowFlags_NoCollapse);
    ImGui::Text("Thread ID: %lu", ::GetCurrentThreadId());
    ImGui::TextDisabled("Press F8 to hide this overlay");
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
        const auto& state = *stateCopy;
        const double ageSeconds = updatedAtMs > 0 && nowMs > updatedAtMs
            ? static_cast<double>(nowMs - updatedAtMs) / 1000.0
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
        const auto displayCount = std::min<std::size_t>(state.route.size(), maxRows);
        for (std::size_t i = 0; i < displayCount; ++i)
        {
            const auto& node = state.route[i];
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

        if (state.player_marker.has_value())
        {
            ImGui::Separator();
            const auto& marker = *state.player_marker;
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
            for (const auto& highlight : state.highlighted_systems)
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
            const auto& pose = *state.camera_pose;
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
            for (const auto& hint : state.hud_hints)
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

    if (!eventWriterReady_.load())
    {
        eventWriterReady_.store(eventWriter_.ensure());
    }

    if (eventWriterReady_.load())
    {
        ImGui::Separator();
        if (ImGui::Button("Send waypoint advance event"))
        {
            overlay::OverlayEvent event;
            event.type = overlay::OverlayEventType::WaypointAdvanced;
            event.payload = nlohmann::json{{"source", "overlay"}, {"sent_ms", nowMs}}.dump();
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
