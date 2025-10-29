#pragma once

#include "helper_server.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>

#include "event_channel.hpp"
#include "log_watcher.hpp"
#include "system_resolver.hpp"
#include "overlay_schema.hpp"
#include "star_catalog.hpp"
#include "session_tracker.hpp"

class HelperRuntime
{
public:
    struct Config
    {
        std::string host{"127.0.0.1"};
        int port{38765};
        std::string token;
        std::filesystem::path executableDirectory;
    };

    struct Status
    {
        bool serverRunning{false};
        bool hasOverlayState{false};
        std::uint64_t eventsRecorded{0};
        std::uint32_t eventsDropped{0};
        std::size_t eventsBuffered{0};
        std::optional<std::chrono::system_clock::time_point> lastSamplePostedAt;
        std::optional<std::chrono::system_clock::time_point> lastInjectionAt;
        bool lastInjectionSuccess{false};
        std::string lastErrorMessage;
        std::string lastInjectionMessage;
        std::optional<std::chrono::system_clock::time_point> lastOverlayAcceptedAt;
        std::optional<std::chrono::system_clock::time_point> lastOverlayGeneratedAt;
        std::filesystem::path chatLogDirectory;
        std::filesystem::path chatLogFile;
        std::filesystem::path combatLogDirectory;
        std::filesystem::path combatLogFile;
        std::optional<helper::logs::LocationSample> location;
        std::optional<helper::logs::CombatSample> combat;
        helper::logs::TelemetrySummary telemetry;
        bool logWatcherRunning{false};
        std::string logWatcherError;
        bool starCatalogLoaded{false};
        std::filesystem::path starCatalogPath;
        std::uint16_t starCatalogVersion{0};
        std::uint32_t starCatalogRecords{0};
    overlay::Vec3f starCatalogBboxMin{};
    overlay::Vec3f starCatalogBboxMax{};
        std::string starCatalogError;
        std::optional<std::chrono::system_clock::time_point> lastTelemetryResetAt;
        bool followModeEnabled{true};
    };

    explicit HelperRuntime(Config config);
    ~HelperRuntime();

    HelperRuntime(const HelperRuntime&) = delete;
    HelperRuntime& operator=(const HelperRuntime&) = delete;

    bool start();
    void stop();
    bool isRunning() const noexcept;

    Status getStatus() const;

    bool postSampleOverlayState();
    bool injectOverlay(const std::wstring& processName = L"exefile.exe");
    std::optional<helper::logs::TelemetrySummary> resetTelemetrySession();
    void saveMiningSession();
    void loadMiningSession();

    HelperServer& server() noexcept { return server_; }
    const HelperServer& server() const noexcept { return server_; }

    helper::SessionTracker* sessionTracker() noexcept { return sessionTracker_.get(); }
    const helper::SessionTracker* sessionTracker() const noexcept { return sessionTracker_.get(); }

private:
    void eventPump();
    overlay::OverlayState buildSampleOverlayState() const;
    std::filesystem::path resolveArtifact(const std::filesystem::path& relative) const;
    void setError(std::string message) const;
    void setInjectionMessage(std::string message, bool success);
    void loadStarCatalog();
    bool applyFollowModeSetting(bool enabled, std::string_view source);

    Config config_;
    HelperServer server_;
    overlay::OverlayEventReader eventReader_;

    std::unique_ptr<helper::logs::LogWatcher> logWatcher_;
    std::optional<helper::logs::LogWatcherStatus> lastLogWatcherStatus_;

    mutable std::mutex statusMutex_;
    std::optional<std::chrono::system_clock::time_point> lastSampleAt_;
    std::optional<std::chrono::system_clock::time_point> lastInjectionAt_;
    std::optional<std::chrono::system_clock::time_point> lastTelemetryResetAt_;
    mutable std::string lastError_;
    mutable std::string lastInjectionMessage_;
    mutable bool lastInjectionSuccess_{false};
    std::optional<overlay::StarCatalog> starCatalog_;
    std::filesystem::path starCatalogPath_;
    std::string starCatalogError_;

    std::filesystem::path executableDirectory_;
    std::filesystem::path artifactRoot_;

    std::thread eventThread_;
    std::atomic_bool running_{false};
    std::atomic_bool stopRequested_{false};
    mutable std::mutex eventMutex_;
    std::condition_variable eventCv_;

    helper::logs::SystemResolver systemResolver_;
    std::atomic_bool followModeEnabled_{true};
    std::unique_ptr<helper::SessionTracker> sessionTracker_;
};
