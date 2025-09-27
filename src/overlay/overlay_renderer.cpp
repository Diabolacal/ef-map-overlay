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
                    spdlog::debug("Overlay state updated from shared memory (version={}, updated_at={})", lastVersion_, lastUpdatedAtMs_);
                }
                catch (const std::exception& ex)
                {
                    currentState_.reset();
                    lastError_ = ex.what();
                    lastPayload_ = snapshot->json_payload;
                    lastUpdatedAtMs_ = snapshot->updated_at_ms;
                    lastVersion_ = snapshot->version;
                    spdlog::error("Failed to parse overlay state from shared memory: {}", ex.what());
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
    }

    if (!visible_.load())
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

    ImGui::SetNextWindowSize(ImVec2(320.0f, 0.0f), ImGuiCond_FirstUseEver);
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
