#include "helper_runtime.hpp"

#include <windows.h>
#include <tlhelp32.h>

#include <cwctype>

#include <spdlog/spdlog.h>

#include <chrono>
#include <filesystem>
#include <string>
#include <utility>
#include <sstream>
#include <system_error>
#include <cctype>
#include <nlohmann/json.hpp>

namespace
{
    std::wstring to_lower_copy(std::wstring value)
    {
        for (auto& ch : value)
        {
            ch = static_cast<wchar_t>(::towlower(ch));
        }
        return value;
    }

    std::string narrow_utf8(const std::wstring& value)
    {
        if (value.empty())
        {
            return {};
        }

        int required = ::WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, nullptr, 0, nullptr, nullptr);
        if (required <= 0)
        {
            return {};
        }

        std::string result(static_cast<std::size_t>(required) - 1, '\0');
        if (::WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, result.data(), required, nullptr, nullptr) <= 0)
        {
            return {};
        }

        return result;
    }

    struct ProcessLookup
    {
        std::optional<DWORD> pid;
        std::size_t matches{0};
        DWORD lastError{0};
    };

    ProcessLookup find_process_by_name(const std::wstring& name)
    {
        ProcessLookup result;

        const std::wstring needle = to_lower_copy(name);

        HANDLE snapshot = ::CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snapshot == INVALID_HANDLE_VALUE)
        {
            result.lastError = ::GetLastError();
            return result;
        }

        PROCESSENTRY32W entry{};
        entry.dwSize = sizeof(PROCESSENTRY32W);

        if (!::Process32FirstW(snapshot, &entry))
        {
            result.lastError = ::GetLastError();
            ::CloseHandle(snapshot);
            return result;
        }

        do
        {
            const std::wstring processName = to_lower_copy(entry.szExeFile);
            if (processName == needle)
            {
                ++result.matches;
                result.pid = entry.th32ProcessID;
                if (result.matches > 1)
                {
                    break;
                }
            }
        } while (::Process32NextW(snapshot, &entry));

        ::CloseHandle(snapshot);
        return result;
    }

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

    std::string normalize_bucket_id(const std::string& label)
    {
        std::string id;
        id.reserve(label.size());
        for (char ch : label)
        {
            if (std::isalnum(static_cast<unsigned char>(ch)))
            {
                id.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
            }
            else if (!id.empty() && id.back() != '-')
            {
                id.push_back('-');
            }
        }

        while (!id.empty() && id.back() == '-')
        {
            id.pop_back();
        }

        if (id.empty())
        {
            id = "resource";
        }

        return id;
    }

    nlohmann::json telemetry_metrics_json(const helper::logs::TelemetrySummary& summary)
    {
        nlohmann::json metrics = nlohmann::json::object();

        if (summary.combat.has_value())
        {
            const auto& combat = *summary.combat;
            nlohmann::json combatJson = {
                {"total_damage_dealt", combat.totalDamageDealt},
                {"total_damage_taken", combat.totalDamageTaken},
                {"recent_damage_dealt", combat.recentDamageDealt},
                {"recent_damage_taken", combat.recentDamageTaken},
                {"recent_window_seconds", combat.recentWindowSeconds},
                {"last_event_ms", combat.lastEventMs}
            };
            metrics["combat"] = std::move(combatJson);
        }

        if (summary.mining.has_value())
        {
            const auto& mining = *summary.mining;
            nlohmann::json miningJson = {
                {"total_volume_m3", mining.totalVolumeM3},
                {"recent_volume_m3", mining.recentVolumeM3},
                {"recent_window_seconds", mining.recentWindowSeconds},
                {"last_event_ms", mining.lastEventMs},
                {"session_start_ms", mining.sessionStartMs},
                {"session_duration_seconds", mining.sessionDurationSeconds}
            };

            if (!mining.buckets.empty())
            {
                nlohmann::json buckets = nlohmann::json::array();
                buckets.get_ref<nlohmann::json::array_t&>().reserve(mining.buckets.size());
                for (const auto& bucket : mining.buckets)
                {
                    buckets.push_back({
                        {"id", normalize_bucket_id(bucket.resource)},
                        {"label", bucket.resource},
                        {"session_total_m3", bucket.sessionTotalM3},
                        {"recent_total_m3", bucket.recentVolumeM3}
                    });
                }
                miningJson["buckets"] = std::move(buckets);
            }

            metrics["mining"] = std::move(miningJson);
        }

        if (summary.history.has_value())
        {
            const auto& history = *summary.history;
            nlohmann::json historyJson = {
                {"slice_seconds", history.sliceSeconds},
                {"capacity", history.capacity},
                {"saturated", history.saturated}
            };

            if (!history.resetMarkersMs.empty())
            {
                historyJson["reset_markers_ms"] = history.resetMarkersMs;
            }

            if (!history.slices.empty())
            {
                nlohmann::json slices = nlohmann::json::array();
                slices.get_ref<nlohmann::json::array_t&>().reserve(history.slices.size());
                for (const auto& slice : history.slices)
                {
                    slices.push_back({
                        {"start_ms", slice.startMs},
                        {"duration_seconds", slice.durationSeconds},
                        {"damage_dealt", slice.damageDealt},
                        {"damage_taken", slice.damageTaken},
                        {"mining_volume_m3", slice.miningVolumeM3}
                    });
                }
                historyJson["slices"] = std::move(slices);
            }

            metrics["history"] = std::move(historyJson);
        }

        return metrics;
    }

    nlohmann::json telemetry_summary_payload(const helper::logs::TelemetrySummary& summary)
    {
        nlohmann::json payload{
            {"status", "ok"},
            {"generated_at_ms", now_ms()}
        };

        auto metrics = telemetry_metrics_json(summary);
        for (auto& item : metrics.items())
        {
            payload[item.key()] = std::move(item.value());
        }

        return payload;
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
            },
            [this]() {
                return followModeEnabled_.load();
            });
    }

    if (logWatcher_)
    {
        logWatcher_->setFollowModeSupplier([this]() {
            return followModeEnabled_.load();
        });
    }

    server_.setTelemetrySummaryHandler([this]() -> std::optional<nlohmann::json> {
        if (!logWatcher_)
        {
            return std::nullopt;
        }
        auto summary = logWatcher_->telemetrySnapshot();
        return telemetry_summary_payload(summary);
    });

    server_.setTelemetryResetHandler([this]() -> std::optional<nlohmann::json> {
        if (!logWatcher_)

    server_.setFollowModeProvider([this]() {
        return followModeEnabled_.load();
    });

    server_.setFollowModeUpdateHandler([this](bool enabled) {
        return applyFollowModeSetting(enabled, "http");
    });
        {
            return std::nullopt;
        }
        const auto resetTimePoint = std::chrono::system_clock::now();
        auto summary = logWatcher_->resetTelemetrySession();
        const auto resetMs = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(resetTimePoint.time_since_epoch()).count());
        {
            std::lock_guard<std::mutex> guard(statusMutex_);
            lastTelemetryResetAt_ = resetTimePoint;
            lastError_.clear();
            lastLogWatcherStatus_ = logWatcher_->status();
        }
        nlohmann::json telemetry = telemetry_metrics_json(summary);
        telemetry["generated_at_ms"] = resetMs;
        nlohmann::json response{
            {"status", "ok"},
            {"reset_ms", resetMs},
            {"telemetry", std::move(telemetry)}
        };
        return response;
    });

    logWatcher_->start();
    {
        std::lock_guard<std::mutex> guard(statusMutex_);
        lastLogWatcherStatus_ = logWatcher_->status();
        lastTelemetryResetAt_.reset();
    }

    loadStarCatalog();

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

    server_.publishOfflineState();

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
            status.telemetry = lastLogWatcherStatus_->telemetry;
            status.logWatcherRunning = lastLogWatcherStatus_->running;
            status.logWatcherError = lastLogWatcherStatus_->lastError;
        }
        else
        {
            status.logWatcherRunning = false;
        }

        status.starCatalogPath = starCatalogPath_;
        status.starCatalogError = starCatalogError_;
        if (starCatalog_)
        {
            status.starCatalogLoaded = true;
            status.starCatalogVersion = starCatalog_->version;
            status.starCatalogRecords = static_cast<std::uint32_t>(starCatalog_->records.size());
            status.starCatalogBboxMin = starCatalog_->bbox_min;
            status.starCatalogBboxMax = starCatalog_->bbox_max;
        }
        else
        {
            status.starCatalogLoaded = false;
            status.starCatalogVersion = 0;
            status.starCatalogRecords = 0;
            status.starCatalogBboxMin = overlay::Vec3f{};
            status.starCatalogBboxMax = overlay::Vec3f{};
        }

        status.lastTelemetryResetAt = lastTelemetryResetAt_;
    }

        status.followModeEnabled = followModeEnabled_.load();

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

    ProcessLookup lookup = find_process_by_name(processName);
    if (lookup.matches == 0 || !lookup.pid.has_value())
    {
        std::ostringstream oss;
        if (lookup.lastError != 0)
        {
            oss << "Failed to enumerate processes (error " << lookup.lastError << ")";
        }
        else
        {
            oss << "Process '" << narrow_utf8(processName) << "' not found";
        }
        setInjectionMessage(oss.str(), false);
        return false;
    }

    if (lookup.matches > 1)
    {
        std::ostringstream oss;
    oss << "Multiple '" << narrow_utf8(processName) << "' processes found ("
            << lookup.matches << "); aborting injection";
        setInjectionMessage(oss.str(), false);
        return false;
    }

    const std::wstring pidArgument = std::to_wstring(*lookup.pid);

    std::wstring commandLine = L"\"" + injectorPath.wstring() + L"\" " + pidArgument + L" \"" + dllPath.wstring() + L"\"";

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

    {
        std::ostringstream oss;
        oss << "Overlay injector completed successfully (PID=" << *lookup.pid << ")";
        setInjectionMessage(oss.str(), true);
    }
    return true;
}

void HelperRuntime::eventPump()
{
    while (!stopRequested_.load())
    {
        if (auto drained = eventReader_.drain(); !drained.events.empty() || drained.dropped > 0)
        {
            for (const auto& event : drained.events)
            {
                if (event.type == overlay::OverlayEventType::FollowModeToggled)
                {
                    bool desired = !followModeEnabled_.load();
                    if (!event.payload.empty())
                    {
                        try
                        {
                            const auto json = nlohmann::json::parse(event.payload);
                            if (json.contains("enabled"))
                            {
                                desired = json.at("enabled").get<bool>();
                            }
                            else if (json.contains("requested"))
                            {
                                const bool requested = json.at("requested").get<bool>();
                                if (!requested)
                                {
                                    desired = followModeEnabled_.load();
                                }
                            }
                        }
                        catch (const std::exception& ex)
                        {
                            spdlog::debug("Failed to parse follow toggle payload: {}", ex.what());
                        }
                    }

                    applyFollowModeSetting(desired, "event");
                }
            }

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
    state.heartbeat_ms = state.generated_at_ms;
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

    state.follow_mode_enabled = followModeEnabled_.load();
    state.active_route_node_id = state.route.size() > 1 ? std::optional<std::string>{state.route[1].system_id} : std::nullopt;
    state.source_online = true;

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

void HelperRuntime::loadStarCatalog()
{
    HelperServer::StarCatalogSummary summary;

    const auto catalogPath = resolveArtifact(std::filesystem::path(L"data/star_catalog_v1.bin"));
    summary.path = catalogPath;

    std::optional<overlay::StarCatalog> loadedCatalog;

    if (catalogPath.empty())
    {
        summary.error = "Catalog path could not be resolved";
    }
    else
    {
        std::error_code ec;
        const bool exists = std::filesystem::exists(catalogPath, ec);
        if (ec)
        {
            summary.error = "Catalog path check failed: " + ec.message();
        }
        else if (!exists)
        {
            summary.error = "Catalog file not found: " + catalogPath.string();
        }
        else
        {
            try
            {
                auto catalog = overlay::load_star_catalog_from_file(catalogPath);
                summary.loaded = true;
                summary.version = catalog.version;
                summary.record_count = static_cast<std::uint32_t>(catalog.records.size());
                summary.bbox_min = catalog.bbox_min;
                summary.bbox_max = catalog.bbox_max;
                loadedCatalog = std::move(catalog);
            }
            catch (const std::exception& ex)
            {
                summary.error = ex.what();
            }
        }
    }

    {
        std::lock_guard<std::mutex> guard(statusMutex_);
        starCatalogPath_ = catalogPath;
        starCatalogError_ = summary.error;
        if (loadedCatalog)
        {
            starCatalog_ = std::move(*loadedCatalog);
        }
        else
        {
            starCatalog_.reset();
        }
    }

    if (summary.loaded)
    {
        spdlog::info("Star catalog loaded from {} (records={}, version={})",
            catalogPath.string(),
            summary.record_count,
            summary.version);
    }
    else
    {
        const auto& message = summary.error.empty() ? std::string{"Unknown error"} : summary.error;
        spdlog::warn("Star catalog unavailable: {}", message);
    }

    server_.updateStarCatalogSummary(std::move(summary));
}

std::optional<helper::logs::TelemetrySummary> HelperRuntime::resetTelemetrySession()
{
    if (!isRunning())
    {
        if (!start())
        {
            return std::nullopt;
        }
    }

    if (!logWatcher_)
    {
        setError("Telemetry reset unavailable (log watcher offline)");
        return std::nullopt;
    }

    const auto resetTimePoint = std::chrono::system_clock::now();
    auto summary = logWatcher_->resetTelemetrySession();
    {
        std::lock_guard<std::mutex> guard(statusMutex_);
        lastTelemetryResetAt_ = resetTimePoint;
        lastError_.clear();
        lastLogWatcherStatus_ = logWatcher_->status();
    }

    spdlog::info("Telemetry session reset via helper runtime");
    return summary;
}

bool HelperRuntime::applyFollowModeSetting(bool enabled, std::string_view source)
{
    const bool previous = followModeEnabled_.exchange(enabled);
    const bool changed = previous != enabled;

    if (changed)
    {
        spdlog::info("Follow mode {} via {}", enabled ? "enabled" : "disabled", source);
    }
    else
    {
        spdlog::debug("Follow mode already {} (source: {})", enabled ? "enabled" : "disabled", source);
    }

    if (changed && !server_.updateFollowModeFlag(enabled))
    {
        spdlog::debug("Follow mode update deferred; overlay state not yet available");
    }

    return changed;
}