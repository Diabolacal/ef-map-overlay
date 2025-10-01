#include "helper_runtime.hpp"

#include <windows.h>

#include <spdlog/spdlog.h>

#include <chrono>
#include <filesystem>
#include <utility>
#include <sstream>

namespace
{
    std::filesystem::path canonical_parent(const std::filesystem::path& path)
    {
        std::error_code ec;
        const auto parent = path.parent_path();
        if (parent.empty())
        {
            return path;
        }
        auto canonical = std::filesystem::weakly_canonical(parent, ec);
        if (ec)
        {
            return parent;
        }
        return canonical;
    }

    std::uint64_t now_ms()
    {
        const auto now = std::chrono::system_clock::now();
        return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count());
    }
}

HelperRuntime::HelperRuntime(Config config)
    : server_(config.host, config.port, config.token)
    , executableDirectory_(config.executableDirectory)
{
    if (executableDirectory_.empty())
    {
        executableDirectory_ = std::filesystem::current_path();
    }

    auto helperDir = canonical_parent(executableDirectory_);
    if (!helperDir.empty())
    {
        auto buildSrc = canonical_parent(helperDir);
        if (!buildSrc.empty())
        {
            artifactRoot_ = buildSrc;
        }
    }

    config_ = std::move(config);
}

HelperRuntime::~HelperRuntime()
{
    stop();
}

bool HelperRuntime::start()
{
    if (running_.load())
    {
        return true;
    }

    if (!server_.start())
    {
        setError("Failed to bind helper HTTP server");
        return false;
    }

    stopRequested_.store(false);
    running_.store(true);

    eventThread_ = std::thread([this]() {
        eventPump();
    });

    if (!logWatcher_)
    {
        helper::logs::LogWatcher::Config watcherConfig{};
        logWatcher_ = std::make_unique<helper::logs::LogWatcher>(
            std::move(watcherConfig),
            systemResolver_,
            [this](const overlay::OverlayState& state, std::size_t payloadBytes) {
                if (!server_.ingestOverlayState(state, payloadBytes, "log-watcher"))
                {
                    setError("Failed to publish overlay state from log watcher");
                }
                else
                {
                    std::lock_guard<std::mutex> guard(statusMutex_);
                    lastSampleAt_ = std::chrono::system_clock::now();
                    lastError_.clear();
                }
            },
            [this](const helper::logs::LogWatcherStatus& status) {
                std::lock_guard<std::mutex> guard(statusMutex_);
                lastLogWatcherStatus_ = status;
            });
    }

    logWatcher_->start();
    {
        std::lock_guard<std::mutex> guard(statusMutex_);
        lastLogWatcherStatus_ = logWatcher_->status();
    }

    spdlog::info("Helper runtime started ({}:{})", server_.host(), server_.port());
    return true;
}

void HelperRuntime::stop()
{
    if (!running_.load())
    {
        return;
    }

    if (logWatcher_)
    {
        logWatcher_->stop();
    }

    stopRequested_.store(true);
    eventCv_.notify_all();

    server_.stop();

    if (eventThread_.joinable())
    {
        eventThread_.join();
    }

    running_.store(false);
    spdlog::info("Helper runtime stopped");
}

bool HelperRuntime::isRunning() const noexcept
{
    return running_.load();
}

HelperRuntime::Status HelperRuntime::getStatus() const
{
    Status status;
    status.serverRunning = server_.isRunning();
    status.hasOverlayState = server_.hasOverlayState();

    const auto stats = server_.getOverlayEventStats();
    status.eventsRecorded = stats.recorded;
    status.eventsDropped = stats.dropped;
    status.eventsBuffered = stats.buffered;

    const auto overlayStats = server_.getOverlayStateStats();
    if (overlayStats.hasState)
    {
        status.lastOverlayAcceptedAt = overlayStats.acceptedAt;
        if (overlayStats.generatedAtMs != 0)
        {
            status.lastOverlayGeneratedAt = std::chrono::system_clock::time_point{std::chrono::milliseconds{overlayStats.generatedAtMs}};
        }
    }

    {
        std::lock_guard<std::mutex> guard(statusMutex_);
        status.lastSamplePostedAt = lastSampleAt_;
        status.lastInjectionAt = lastInjectionAt_;
        status.lastErrorMessage = lastError_;
        status.lastInjectionMessage = lastInjectionMessage_;
        status.lastInjectionSuccess = lastInjectionSuccess_;
        if (lastLogWatcherStatus_.has_value())
        {
            status.chatLogDirectory = lastLogWatcherStatus_->chatDirectory;
            status.chatLogFile = lastLogWatcherStatus_->chatFile;
            status.combatLogDirectory = lastLogWatcherStatus_->combatDirectory;
            status.combatLogFile = lastLogWatcherStatus_->combatFile;
            status.location = lastLogWatcherStatus_->location;
            status.combat = lastLogWatcherStatus_->combat;
            status.logWatcherRunning = lastLogWatcherStatus_->running;
            status.logWatcherError = lastLogWatcherStatus_->lastError;
        }
        else
        {
            status.logWatcherRunning = false;
        }
    }

    return status;
}

bool HelperRuntime::postSampleOverlayState()
{
    if (!isRunning())
    {
        if (!start())
        {
            return false;
        }
    }

    const auto state = buildSampleOverlayState();
    const auto json = overlay::serialize_overlay_state(state).dump();

    if (!server_.ingestOverlayState(state, json.size(), "tray-sample"))
    {
        setError("Failed to publish sample overlay state");
        return false;
    }

    {
        std::lock_guard<std::mutex> guard(statusMutex_);
        lastSampleAt_ = std::chrono::system_clock::now();
        lastError_.clear();
    }

    spdlog::info("Sample overlay state posted via tray action");
    return true;
}

bool HelperRuntime::injectOverlay(const std::wstring& processName)
{
    const auto injectorPath = resolveArtifact(std::filesystem::path(L"injector/Release/ef-overlay-injector.exe"));
    const auto dllPath = resolveArtifact(std::filesystem::path(L"overlay/Release/ef-overlay.dll"));

    if (injectorPath.empty() || dllPath.empty())
    {
        setInjectionMessage("Overlay injector artifacts not found", false);
        return false;
    }

    if (!std::filesystem::exists(injectorPath) || !std::filesystem::exists(dllPath))
    {
        std::ostringstream oss;
        oss << "Injector or overlay DLL missing (" << injectorPath.string() << ", " << dllPath.string() << ")";
        setInjectionMessage(oss.str(), false);
        return false;
    }

    std::wstring commandLine = L"\"" + injectorPath.wstring() + L"\" " + processName + L" \"" + dllPath.wstring() + L"\"";

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};

    std::wstring commandMutable = commandLine;
    if (!CreateProcessW(nullptr, commandMutable.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi))
    {
        const auto errorCode = GetLastError();
        std::ostringstream oss;
        oss << "CreateProcessW failed with error " << errorCode;
        setInjectionMessage(oss.str(), false);
        return false;
    }

    WaitForSingleObject(pi.hProcess, INFINITE);

    DWORD exitCode = 1;
    if (!GetExitCodeProcess(pi.hProcess, &exitCode))
    {
        exitCode = 1;
    }

    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    if (exitCode != 0)
    {
        std::ostringstream oss;
        oss << "Injector exited with code " << exitCode;
        setInjectionMessage(oss.str(), false);
        return false;
    }

    setInjectionMessage("Overlay injector completed successfully", true);
    return true;
}

void HelperRuntime::eventPump()
{
    while (!stopRequested_.load())
    {
        if (auto drained = eventReader_.drain(); !drained.events.empty() || drained.dropped > 0)
        {
            server_.recordOverlayEvents(std::move(drained.events), drained.dropped);
        }

        std::unique_lock<std::mutex> lock(eventMutex_);
        eventCv_.wait_for(lock, std::chrono::seconds(1), [this]() { return stopRequested_.load(); });
    }
}

overlay::OverlayState HelperRuntime::buildSampleOverlayState() const
{
    overlay::OverlayState state;
    state.generated_at_ms = now_ms();
    state.notes = "Tray sample route";

    state.route = {
        overlay::RouteNode{
            .system_id = "TRAY-START",
            .display_name = "Tray Entry",
            .distance_ly = 0.0,
            .via_gate = false
        },
        overlay::RouteNode{
            .system_id = "TRAY-MID",
            .display_name = "Tray Waypoint",
            .distance_ly = 4.2,
            .via_gate = true
        },
        overlay::RouteNode{
            .system_id = "TRAY-END",
            .display_name = "Tray Destination",
            .distance_ly = 9.4,
            .via_gate = false
        }
    };

    state.player_marker = overlay::PlayerMarker{
        .system_id = "TRAY-MID",
        .display_name = "Tray Test Pilot",
        .is_docked = false
    };

    state.highlighted_systems = {
        overlay::HighlightedSystem{
            .system_id = "TRAY-MID",
            .display_name = "Safe Unload",
            .category = "info",
            .note = std::optional<std::string>{"Allied presence detected"}
        },
        overlay::HighlightedSystem{
            .system_id = "TRAY-END",
            .display_name = "Hostile Fleet",
            .category = "warning",
            .note = std::optional<std::string>{"Scout confirmed 5+ battleships"}
        }
    };

    state.camera_pose = overlay::CameraPose{
        .position = overlay::Vec3f{15.0f, 8.5f, -12.0f},
        .look_at = overlay::Vec3f{0.0f, 0.0f, 0.0f},
        .up = overlay::Vec3f{0.0f, 1.0f, 0.0f},
        .fov_degrees = 55.0f
    };

    state.hud_hints = {
        overlay::HudHint{
            .id = "tray_follow_toggle",
            .text = "Press F8 to hide the overlay",
            .dismissible = true,
            .active = true
        },
        overlay::HudHint{
            .id = "tray_route_progress",
            .text = "Next gate: Tray Waypoint",
            .dismissible = false,
            .active = true
        }
    };

    state.follow_mode_enabled = true;
    state.active_route_node_id = state.route.size() > 1 ? std::optional<std::string>{state.route[1].system_id} : std::nullopt;

    return state;
}

std::filesystem::path HelperRuntime::resolveArtifact(const std::filesystem::path& relative) const
{
    if (relative.empty())
    {
        return {};
    }

    if (!artifactRoot_.empty())
    {
        return artifactRoot_ / relative;
    }

    return executableDirectory_ / relative;
}

void HelperRuntime::setError(std::string message) const
{
    spdlog::error("{}", message);
    std::lock_guard<std::mutex> guard(statusMutex_);
    lastError_ = std::move(message);
}

void HelperRuntime::setInjectionMessage(std::string message, bool success)
{
    if (success)
    {
        spdlog::info("{}", message);
    }
    else
    {
        spdlog::error("{}", message);
    }
    {
        std::lock_guard<std::mutex> guard(statusMutex_);
        lastInjectionMessage_ = std::move(message);
        lastInjectionSuccess_ = success;
        lastInjectionAt_ = std::chrono::system_clock::now();
        if (!success)
        {
            lastError_ = lastInjectionMessage_;
        }
    }
}