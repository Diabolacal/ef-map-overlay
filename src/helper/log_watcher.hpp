#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>
#include <map>

#include "overlay_schema.hpp"
#include "system_resolver.hpp"

namespace helper::logs
{
    struct LocationSample
    {
        std::string systemName;
        std::string systemId;
        std::chrono::system_clock::time_point observedAt;
    };

    struct CombatSample
    {
        std::string characterId;
        std::chrono::system_clock::time_point lastEventAt;
        std::uint64_t combatEventCount{0};
        std::uint64_t notifyEventCount{0};
        std::string lastCombatLine;
    };

    struct CombatTelemetrySnapshot
    {
        double totalDamageDealt{0.0};
        double totalDamageTaken{0.0};
        double recentDamageDealt{0.0};
        double recentDamageTaken{0.0};
        double recentWindowSeconds{30.0};
        std::uint64_t lastEventMs{0};
        std::uint64_t sessionStartMs{0};
        double sessionDurationSeconds{0.0};
        
        // Hit quality counters (dealt)
        std::uint64_t missDealt{0};
        std::uint64_t glancingDealt{0};
        std::uint64_t standardDealt{0};
        std::uint64_t penetratingDealt{0};
        std::uint64_t smashingDealt{0};
        
        // Hit quality counters (taken)
        std::uint64_t missTaken{0};
        std::uint64_t glancingTaken{0};
        std::uint64_t standardTaken{0};
        std::uint64_t penetratingTaken{0};
        std::uint64_t smashingTaken{0};
        
        [[nodiscard]] bool hasData() const
        {
            return totalDamageDealt > 0.0 || totalDamageTaken > 0.0 || lastEventMs != 0 || sessionStartMs != 0;
        }
    };

    struct MiningBucketSnapshot
    {
        std::string resource;
        double sessionTotalM3{0.0};
        double recentVolumeM3{0.0};
    };

    struct MiningTelemetrySnapshot
    {
        double totalVolumeM3{0.0};
        double recentVolumeM3{0.0};
        double recentWindowSeconds{120.0};
        std::uint64_t lastEventMs{0};
        std::uint64_t sessionStartMs{0};
        double sessionDurationSeconds{0.0};
        std::vector<MiningBucketSnapshot> buckets;
        [[nodiscard]] bool hasData() const
        {
            return totalVolumeM3 > 0.0 || lastEventMs != 0 || sessionStartMs != 0;
        }
    };

    struct TelemetryHistorySliceSnapshot
    {
        std::uint64_t startMs{0};
        double durationSeconds{0.0};
        double damageDealt{0.0};
        double damageTaken{0.0};
        double miningVolumeM3{0.0};
    };

    struct TelemetryHistorySnapshot
    {
        double sliceSeconds{300.0};
        std::uint32_t capacity{0};
        bool saturated{false};
        std::vector<TelemetryHistorySliceSnapshot> slices;
        std::vector<std::uint64_t> resetMarkersMs;
        [[nodiscard]] bool hasData() const
        {
            return !slices.empty();
        }
    };

    // High-granularity sparkline samples (~1s resolution, 120s retention)
    struct CombatDamageSample
    {
        std::uint64_t timestampMs{0};
        double damageDealt{0.0};
        double damageTaken{0.0};
    };

    struct MiningRateSample
    {
        std::uint64_t timestampMs{0};
        double volumeM3{0.0};
    };

    struct TelemetrySummary
    {
        std::optional<CombatTelemetrySnapshot> combat;
        std::optional<MiningTelemetrySnapshot> mining;
        std::optional<TelemetryHistorySnapshot> history;
        std::vector<CombatDamageSample> combatSparkline;
        std::vector<MiningRateSample> miningSparkline;
    };

    struct LogWatcherStatus
    {
        bool running{false};
        std::filesystem::path chatDirectory;
        std::filesystem::path chatFile;
        std::filesystem::path combatDirectory;
        std::filesystem::path combatFile;
        std::optional<LocationSample> location;
        std::optional<CombatSample> combat;
        TelemetrySummary telemetry;
        std::string lastError;
    };

    class LogWatcher
    {
    public:
        struct Config
        {
            std::optional<std::filesystem::path> chatDirectoryOverride;
            std::optional<std::filesystem::path> combatDirectoryOverride;
            std::chrono::milliseconds pollInterval{std::chrono::milliseconds{750}};
        };

        using PublishCallback = std::function<void(const overlay::OverlayState& state, std::size_t payloadBytes)>;
        using StatusCallback = std::function<void(const LogWatcherStatus& status)>;
        using FollowModeSupplier = std::function<bool()>;

        LogWatcher(Config config, const SystemResolver& resolver, PublishCallback publishCallback, StatusCallback statusCallback, FollowModeSupplier followSupplier = {});
        ~LogWatcher();

        LogWatcher(const LogWatcher&) = delete;
        LogWatcher& operator=(const LogWatcher&) = delete;

        void start();
        void stop();

        LogWatcherStatus status() const;

    TelemetrySummary telemetrySnapshot();
    TelemetrySummary resetTelemetrySession();
    void restoreMiningSession(const MiningTelemetrySnapshot& persisted);
    void forcePublish();

        void setFollowModeSupplier(FollowModeSupplier supplier);
        
        // Reload log directories from registry (for custom path changes)
        void reloadLogPaths();

    private:
        enum class TextEncoding
        {
            Unknown,
            Utf8,
            Utf16LE
        };

        struct FileTailState
        {
            std::filesystem::path path;
            std::uint64_t offset{0};
            TextEncoding encoding{TextEncoding::Unknown};
            bool consumedBom{false};
            std::string pendingLine;
            std::vector<char> pendingBytes;

            void reset(const std::filesystem::path& newPath)
            {
                path = newPath;
                offset = 0;
                encoding = TextEncoding::Unknown;
                consumedBom = false;
                pendingLine.clear();
                pendingBytes.clear();
            }
        };

        void run();
        bool discoverDirectories();
        bool refreshChatFile();
        bool refreshCombatFile();
        bool processLocalChat();
        bool processCombat();
        std::vector<std::string> readNewLines(FileTailState& state);
        bool ensureUtf16Even(FileTailState& state, std::vector<char>& buffer);
    std::string convertToUtf8(FileTailState& state, std::vector<char>& buffer, bool isFirstChunk);
        std::optional<std::filesystem::path> resolveDefaultDirectory(const wchar_t* subFolder) const;
        std::optional<std::filesystem::path> latestChatLogPath(const std::filesystem::path& directory) const;
        std::optional<std::filesystem::path> latestCombatLogPath(const std::filesystem::path& directory) const;
        void publishStateIfNeeded(const LogWatcherStatus& snapshot, bool forcePublish);
        overlay::OverlayState buildOverlayState(const LogWatcherStatus& snapshot) const;
        static std::string buildStatusNotes(const LogWatcherStatus& snapshot);
        static std::uint64_t now_ms();
        bool followModeEnabled() const;

        Config config_;
    const SystemResolver& resolver_;
    PublishCallback publishCallback_;
    StatusCallback statusCallback_;

    class CombatTelemetryAggregator;
    class MiningTelemetryAggregator;
    class TelemetryHistoryAggregator;
    std::unique_ptr<CombatTelemetryAggregator> combatTelemetryAggregator_;
    std::unique_ptr<MiningTelemetryAggregator> miningTelemetryAggregator_;
    std::unique_ptr<TelemetryHistoryAggregator> telemetryHistoryAggregator_;

        mutable std::mutex mutex_;
        std::condition_variable cv_;
        std::thread worker_;
        std::atomic_bool running_{false};
        std::atomic_bool stopRequested_{false};

        LogWatcherStatus status_;
        FileTailState chatTail_;
        FileTailState combatTail_;
        std::filesystem::file_time_type chatWriteTime_{};
        std::filesystem::file_time_type combatWriteTime_{};
        std::optional<std::string> lastPublishedSystemId_;
        std::chrono::system_clock::time_point lastPublishedAt_{};
        FollowModeSupplier followModeSupplier_{};
    };
}
