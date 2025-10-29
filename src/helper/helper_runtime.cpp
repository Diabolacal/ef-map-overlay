#include "helper_runtime.hpp"

#include <windows.h>
#include <tlhelp32.h>
#include <shlobj.h>
#include <shellapi.h>

#include <cwctype>

#include <spdlog/spdlog.h>

#include <chrono>
#include <filesystem>
#include <string>
#include <utility>
#include <sstream>
#include <system_error>
#include <cctype>
#include <fstream>
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
                {"last_event_ms", combat.lastEventMs},
                {"session_start_ms", combat.sessionStartMs},
                {"session_duration_seconds", combat.sessionDurationSeconds},
                {"miss_dealt", combat.missDealt},
                {"glancing_dealt", combat.glancingDealt},
                {"standard_dealt", combat.standardDealt},
                {"penetrating_dealt", combat.penetratingDealt},
                {"smashing_dealt", combat.smashingDealt},
                {"miss_taken", combat.missTaken},
                {"glancing_taken", combat.glancingTaken},
                {"standard_taken", combat.standardTaken},
                {"penetrating_taken", combat.penetratingTaken},
                {"smashing_taken", combat.smashingTaken}
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
        
        // Serialize high-granularity sparkline buffers (always include, even if empty)
        nlohmann::json combatSamples = nlohmann::json::array();
        if (!summary.combatSparkline.empty())
        {
            combatSamples.get_ref<nlohmann::json::array_t&>().reserve(summary.combatSparkline.size());
            for (const auto& sample : summary.combatSparkline)
            {
                combatSamples.push_back({
                    {"t", sample.timestampMs},
                    {"dd", sample.damageDealt},
                    {"dt", sample.damageTaken}
                });
            }
        }
        metrics["combat_sparkline"] = std::move(combatSamples);
        
        nlohmann::json miningSamples = nlohmann::json::array();
        if (!summary.miningSparkline.empty())
        {
            miningSamples.get_ref<nlohmann::json::array_t&>().reserve(summary.miningSparkline.size());
            for (const auto& sample : summary.miningSparkline)
            {
                miningSamples.push_back({
                    {"t", sample.timestampMs},
                    {"v", sample.volumeM3}
                });
            }
        }
        metrics["mining_sparkline"] = std::move(miningSamples);

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

    std::filesystem::path get_session_persistence_path()
    {
        PWSTR rawPath = nullptr;
        std::filesystem::path result;
        if (SUCCEEDED(::SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_CREATE, nullptr, &rawPath)))
        {
            result = std::filesystem::path(rawPath) / L"EFOverlay" / L"data";
            ::CoTaskMemFree(rawPath);
        }
        else
        {
            result = std::filesystem::temp_directory_path() / L"EFOverlay" / L"data";
        }

        std::error_code ec;
        std::filesystem::create_directories(result, ec);
        return result / "mining_session.json";
    }

    void save_mining_session(const helper::logs::MiningTelemetrySnapshot& mining)
    {
        try
        {
            nlohmann::json j;
            j["version"] = 1;
            j["total_volume_m3"] = mining.totalVolumeM3;
            j["session_start_ms"] = mining.sessionStartMs;
            j["last_event_ms"] = mining.lastEventMs;
            
            nlohmann::json bucketsArray = nlohmann::json::array();
            for (const auto& bucket : mining.buckets)
            {
                nlohmann::json bucketObj;
                bucketObj["resource"] = bucket.resource;
                bucketObj["session_total_m3"] = bucket.sessionTotalM3;
                bucketsArray.push_back(bucketObj);
            }
            j["buckets"] = bucketsArray;

            const auto path = get_session_persistence_path();
            std::ofstream ofs(path);
            if (ofs.is_open())
            {
                ofs << j.dump(2);
                spdlog::debug("Saved mining session to {}", path.string());
            }
        }
        catch (const std::exception& ex)
        {
            spdlog::warn("Failed to save mining session: {}", ex.what());
        }
    }

    std::optional<helper::logs::MiningTelemetrySnapshot> load_mining_session()
    {
        try
        {
            const auto path = get_session_persistence_path();
            if (!std::filesystem::exists(path))
            {
                spdlog::debug("No persisted mining session found at {}", path.string());
                return std::nullopt;
            }

            std::ifstream ifs(path);
            if (!ifs.is_open())
            {
                spdlog::warn("Could not open persisted session file: {}", path.string());
                return std::nullopt;
            }

            nlohmann::json j;
            ifs >> j;

            helper::logs::MiningTelemetrySnapshot snapshot;
            snapshot.totalVolumeM3 = j.value("total_volume_m3", 0.0);
            snapshot.sessionStartMs = j.value("session_start_ms", static_cast<std::uint64_t>(0));
            snapshot.lastEventMs = j.value("last_event_ms", static_cast<std::uint64_t>(0));
            snapshot.recentWindowSeconds = 120.0;

            if (j.contains("buckets") && j["buckets"].is_array())
            {
                for (const auto& bucketJson : j["buckets"])
                {
                    helper::logs::MiningBucketSnapshot bucket;
                    bucket.resource = bucketJson.value("resource", "");
                    bucket.sessionTotalM3 = bucketJson.value("session_total_m3", 0.0);
                    snapshot.buckets.push_back(bucket);
                }
            }

            spdlog::info("Loaded persisted mining session: {:.1f} m³ total, {} buckets", 
                         snapshot.totalVolumeM3, snapshot.buckets.size());
            return snapshot;
        }
        catch (const std::exception& ex)
        {
            spdlog::warn("Failed to load mining session: {}", ex.what());
            return std::nullopt;
        }
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

    // Initialize SessionTracker with data directory
    PWSTR rawPath = nullptr;
    std::filesystem::path dataDir;
    if (SUCCEEDED(::SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_CREATE, nullptr, &rawPath)))
    {
        dataDir = std::filesystem::path(rawPath) / L"EFOverlay" / L"data";
        ::CoTaskMemFree(rawPath);
    }
    else
    {
        dataDir = std::filesystem::temp_directory_path() / L"EFOverlay" / L"data";
    }

    std::error_code ec;
    std::filesystem::create_directories(dataDir, ec);
    sessionTracker_ = std::make_unique<helper::SessionTracker>(dataDir);
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
                // Record system visit if we have location data
                if (state.player_marker.has_value() && !state.player_marker->system_id.empty())
                {
                    const auto& systemId = state.player_marker->system_id;
                    const auto& systemName = state.player_marker->display_name;
                    
                    if (sessionTracker_)
                    {
                        sessionTracker_->recordSystemVisitAllTime(systemId, systemName);
                        sessionTracker_->recordSystemVisitSession(systemId, systemName);
                    }
                }

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

    server_.setInjectOverlayHandler([this]() -> bool {
        return injectOverlay();
    });

    server_.setFollowModeProvider([this]() {
        return followModeEnabled_.load();
    });

    server_.setFollowModeUpdateHandler([this](bool enabled) {
        return applyFollowModeSetting(enabled, "http");
    });

    server_.setSessionTrackerProvider([this]() -> helper::SessionTracker* {
        return sessionTracker_.get();
    });

    // Log path reload handler
    server_.setLogPathReloadHandler([this]() {
        if (logWatcher_)
        {
            logWatcher_->reloadLogPaths();
        }
    });

    // Restore persisted mining session BEFORE starting log processing
    // This ensures session state is loaded before any new mining events are processed
    loadMiningSession();

    logWatcher_->start();
    {
        std::lock_guard<std::mutex> guard(statusMutex_);
        lastLogWatcherStatus_ = logWatcher_->status();
        lastTelemetryResetAt_.reset();
    }

    // Force publish the restored mining session AFTER LogWatcher has started
    // This ensures the publish callback is registered and overlay sees the restored state
    logWatcher_->forcePublish();

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
    // Try MSIX/installed layout first (files in same directory as exe)
    auto injectorPath = executableDirectory_ / L"ef-overlay-injector.exe";
    auto dllPath = executableDirectory_ / L"ef-overlay.dll";
    
    // Fallback to development build layout if MSIX layout doesn't exist
    if (!std::filesystem::exists(injectorPath))
    {
        injectorPath = resolveArtifact(std::filesystem::path(L"injector/Release/ef-overlay-injector.exe"));
        dllPath = resolveArtifact(std::filesystem::path(L"overlay/Release/ef-overlay.dll"));
        
        // Also try Debug build
        if (!std::filesystem::exists(injectorPath))
        {
            injectorPath = resolveArtifact(std::filesystem::path(L"injector/Debug/ef-overlay-injector.exe"));
            dllPath = resolveArtifact(std::filesystem::path(L"overlay/Debug/ef-overlay.dll"));
        }
    }

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

    // MSIX apps install to WindowsApps folder which has ACL restrictions preventing
    // ShellExecuteEx with runas from working. Copy to temp directory as workaround.
    std::filesystem::path actualInjectorPath = injectorPath;
    std::filesystem::path actualDllPath = dllPath;
    
    bool needsTempCopy = injectorPath.wstring().find(L"WindowsApps") != std::wstring::npos;
    std::filesystem::path tempDir;
    
    if (needsTempCopy)
    {
        wchar_t tempPathBuf[MAX_PATH];
        if (GetTempPathW(MAX_PATH, tempPathBuf) == 0)
        {
            setInjectionMessage("Failed to get temp directory path", false);
            return false;
        }
        
        tempDir = std::filesystem::path(tempPathBuf) / L"ef-overlay-inject";
        
        try
        {
            std::filesystem::create_directories(tempDir);
            
            actualInjectorPath = tempDir / L"ef-overlay-injector.exe";
            actualDllPath = tempDir / L"ef-overlay.dll";
            
            std::filesystem::copy_file(injectorPath, actualInjectorPath, std::filesystem::copy_options::overwrite_existing);
            std::filesystem::copy_file(dllPath, actualDllPath, std::filesystem::copy_options::overwrite_existing);
        }
        catch (const std::exception& e)
        {
            std::ostringstream oss;
            oss << "Failed to copy injection files to temp: " << e.what();
            setInjectionMessage(oss.str(), false);
            return false;
        }
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
    
    // Build parameters string: "<pid> <dll-path>" using actualDllPath (possibly temp copy)
    std::wstring parameters = pidArgument + L" \"" + actualDllPath.wstring() + L"\"";
    
    // Store paths as wstrings (not temporaries) so c_str() pointers remain valid
    std::wstring injectorPathStr = actualInjectorPath.wstring();
    std::wstring dllPathStr = actualDllPath.wstring();

    // Use ShellExecuteEx with "runas" verb to elevate injector (triggers UAC prompt)
    SHELLEXECUTEINFOW sei{};
    sei.cbSize = sizeof(SHELLEXECUTEINFOW);
    sei.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC;
    sei.hwnd = nullptr;
    sei.lpVerb = L"runas";  // Request elevation
    sei.lpFile = injectorPathStr.c_str();
    sei.lpParameters = parameters.c_str();
    sei.lpDirectory = nullptr;
    sei.nShow = SW_HIDE;

    if (!ShellExecuteExW(&sei))
    {
        const auto errorCode = GetLastError();
        std::ostringstream oss;
        
        if (errorCode == ERROR_CANCELLED)
        {
            oss << "User cancelled UAC elevation prompt";
        }
        else
        {
            oss << "Failed to launch injector with elevation (error " << errorCode << ")";
        }
        
        setInjectionMessage(oss.str(), false);
        return false;
    }

    if (!sei.hProcess)
    {
        setInjectionMessage("Injector elevation failed (no process handle)", false);
        return false;
    }

    // Wait for injector to complete
    WaitForSingleObject(sei.hProcess, INFINITE);

    DWORD exitCode = 1;
    if (!GetExitCodeProcess(sei.hProcess, &exitCode))
    {
        exitCode = 1;
    }

    CloseHandle(sei.hProcess);

    if (exitCode != 0)
    {
        std::ostringstream oss;
        oss << "Injector exited with code " << exitCode << " - check if game is running";
        setInjectionMessage(oss.str(), false);
        return false;
    }

    {
        std::ostringstream oss;
        oss << "Overlay injected successfully into " << narrow_utf8(processName) << " (PID=" << *lookup.pid << ")";
        setInjectionMessage(oss.str(), true);
    }
    return true;
}

void HelperRuntime::eventPump()
{
    auto lastPersistTime = std::chrono::steady_clock::now();
    constexpr auto kPersistInterval = std::chrono::seconds(30);
    
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
                else if (event.type == overlay::OverlayEventType::VisitedSystemsTrackingToggled)
                {
                    spdlog::info("Received VisitedSystemsTrackingToggled event from overlay");
                    if (sessionTracker_)
                    {
                        const bool currentState = sessionTracker_->isAllTimeTrackingEnabled();
                        sessionTracker_->setAllTimeTrackingEnabled(!currentState);
                        spdlog::info("Toggled visited systems tracking: {} -> {}", currentState, !currentState);
                        
                        // Direct state update like follow mode (instant WebSocket + shared memory broadcast)
                        if (!server_.updateTrackingFlag(!currentState))
                        {
                            spdlog::debug("Tracking flag update deferred; overlay state not yet available");
                        }
                    }
                    else
                    {
                        spdlog::warn("Cannot toggle tracking: sessionTracker not initialized");
                    }
                }
                else if (event.type == overlay::OverlayEventType::SessionStartRequested)
                {
                    spdlog::info("Received SessionStartRequested event from overlay");
                    if (sessionTracker_)
                    {
                        const std::string sessionId = sessionTracker_->startSession();
                        spdlog::info("Started new session: {}", sessionId);
                        
                        // Direct state update like follow mode (instant WebSocket + shared memory broadcast)
                        if (!server_.updateSessionState(true, sessionId))
                        {
                            spdlog::debug("Session state update deferred; overlay state not yet available");
                        }
                    }
                    else
                    {
                        spdlog::warn("Cannot start session: sessionTracker not initialized");
                    }
                }
                else if (event.type == overlay::OverlayEventType::SessionStopRequested)
                {
                    spdlog::info("Received SessionStopRequested event from overlay");
                    if (sessionTracker_)
                    {
                        if (sessionTracker_->hasActiveSession())
                        {
                            sessionTracker_->stopSession();
                            spdlog::info("Stopped active session");
                            
                            // Direct state update like follow mode (instant WebSocket + shared memory broadcast)
                            if (!server_.updateSessionState(false, std::nullopt))
                            {
                                spdlog::debug("Session state update deferred; overlay state not yet available");
                            }
                        }
                        else
                        {
                            spdlog::warn("Cannot stop session: no active session");
                        }
                    }
                    else
                    {
                        spdlog::warn("Cannot stop session: sessionTracker not initialized");
                    }
                }
                else if (event.type == overlay::OverlayEventType::BookmarkCreateRequested)
                {
                    if (!event.payload.empty())
                    {
                        try
                        {
                            const auto json = nlohmann::json::parse(event.payload);
                            const std::string systemId = json.at("system_id").get<std::string>();
                            const std::string notes = json.value("notes", "");
                            const bool forTribe = json.value("for_tribe", false);
                            
                            // Extract system name from current overlay state (player marker)
                            std::string systemName;
                            if (auto stateOpt = server_.getLatestOverlayStateJson(); stateOpt.has_value())
                            {
                                const auto& stateJson = *stateOpt;
                                if (stateJson.contains("player_marker") && stateJson["player_marker"].is_object())
                                {
                                    const auto& marker = stateJson["player_marker"];
                                    systemName = marker.value("display_name", "");
                                }
                            }
                            
                            spdlog::info("Processing bookmark request: system={} ({}), notes={}, for_tribe={}", 
                                         systemId, systemName, notes, forTribe);
                            
                            // POST to helper HTTP endpoint which will relay to EF Map
                            // Use local HTTP client to post to our own /bookmarks/create endpoint
                            // which then forwards to the EF Map worker
                            
                            // Build HTTP request body
                            nlohmann::json requestBody;
                            requestBody["system_id"] = systemId;
                            requestBody["system_name"] = systemName;
                            requestBody["notes"] = notes;
                            requestBody["for_tribe"] = forTribe;
                            
                            // Post to local helper endpoint (async fire-and-forget for now)
                            // The endpoint will handle forwarding to EF Map
                            std::thread([requestBody]() {
                                try
                                {
                                    httplib::Client cli("127.0.0.1", 38765);
                                    cli.set_connection_timeout(2, 0);
                                    cli.set_read_timeout(5, 0);
                                    
                                    httplib::Headers headers = {
                                        {"Content-Type", "application/json"},
                                        {"X-EF-Helper-Auth", "ef-overlay-dev-token-2025"}
                                    };
                                    
                                    const auto res = cli.Post("/bookmarks/create", headers, 
                                                              requestBody.dump(), "application/json");
                                    
                                    if (res && res->status == 200)
                                    {
                                        spdlog::info("Bookmark creation succeeded");
                                    }
                                    else
                                    {
                                        spdlog::warn("Bookmark creation failed: HTTP {}", 
                                                     res ? res->status : 0);
                                    }
                                }
                                catch (const std::exception& ex)
                                {
                                    spdlog::error("Bookmark creation HTTP request failed: {}", ex.what());
                                }
                            }).detach();
                        }
                        catch (const std::exception& ex)
                        {
                            spdlog::error("Failed to parse BookmarkCreateRequested payload: {}", ex.what());
                        }
                    }
                }
                else if (event.type == overlay::OverlayEventType::PscanTriggerRequested)
                {
                    spdlog::info("Received PscanTriggerRequested event from overlay");
                    
                    // Broadcast to web app via WebSocket to trigger scan
                    nlohmann::json wsMessage;
                    wsMessage["type"] = "pscan_trigger_request";
                    wsMessage["timestamp_ms"] = event.timestamp_ms;
                    
                    server_.broadcastWebSocketMessage(wsMessage);
                    spdlog::info("Broadcasted pscan_trigger_request to web app via WebSocket");
                }
                else if (event.type == overlay::OverlayEventType::CustomJson)
                {
                    if (!event.payload.empty())
                    {
                        try
                        {
                            const auto json = nlohmann::json::parse(event.payload);
                            if (json.contains("action"))
                            {
                                const std::string action = json.at("action").get<std::string>();
                                if (action == "telemetry_reset")
                                {
                                    spdlog::info("Received telemetry_reset event from overlay");
                                    if (logWatcher_)
                                    {
                                        // Reset the session
                                        logWatcher_->resetTelemetrySession();
                                        
                                        // Immediately delete persisted session file
                                        try
                                        {
                                            const auto path = get_session_persistence_path();
                                            if (std::filesystem::exists(path))
                                            {
                                                std::filesystem::remove(path);
                                                spdlog::debug("Removed persisted session file after reset");
                                            }
                                        }
                                        catch (const std::exception& ex)
                                        {
                                            spdlog::warn("Failed to remove persisted session file: {}", ex.what());
                                        }
                                        
                                        // Force immediate state publish so overlay updates instantly
                                        logWatcher_->forcePublish();
                                        
                                        spdlog::info("Telemetry session reset completed and published");
                                    }
                                    else
                                    {
                                        spdlog::warn("Cannot reset telemetry: logWatcher not initialized");
                                    }
                                }
                            }
                        }
                        catch (const std::exception& ex)
                        {
                            spdlog::debug("Failed to parse CustomJson event payload: {}", ex.what());
                        }
                    }
                }
            }

            server_.recordOverlayEvents(std::move(drained.events), drained.dropped);
        }

        // Periodically persist mining session
        const auto now = std::chrono::steady_clock::now();
        if (now - lastPersistTime >= kPersistInterval)
        {
            saveMiningSession();
            lastPersistTime = now;
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
    
    // Delete persisted session after reset
    try
    {
        const auto path = get_session_persistence_path();
        if (std::filesystem::exists(path))
        {
            std::filesystem::remove(path);
            spdlog::debug("Removed persisted session file after reset");
        }
    }
    catch (const std::exception& ex)
    {
        spdlog::warn("Failed to remove persisted session file: {}", ex.what());
    }
    
    return summary;
}

void HelperRuntime::saveMiningSession()
{
    if (!logWatcher_)
    {
        return;
    }

    auto status = logWatcher_->status();
    if (status.telemetry.mining.has_value() && status.telemetry.mining->hasData())
    {
        save_mining_session(*status.telemetry.mining);
    }
}

void HelperRuntime::loadMiningSession()
{
    if (!logWatcher_)
    {
        spdlog::error("Cannot load mining session: LogWatcher not initialized");
        return;
    }
    
    auto persisted = load_mining_session();
    if (persisted.has_value())
    {
        spdlog::info("Loaded persisted session: {:.1f} m3, {} buckets, sessionStart={}, lastEvent={}", 
            persisted->totalVolumeM3, 
            persisted->buckets.size(),
            persisted->sessionStartMs,
            persisted->lastEventMs);
        
        logWatcher_->restoreMiningSession(*persisted);
        spdlog::info("Called restoreMiningSession() - session should be restored in LogWatcher");
    }
    else
    {
        spdlog::warn("No persisted mining session to restore (file doesn't exist or failed to parse)");
    }
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