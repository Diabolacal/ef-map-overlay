#include "log_watcher.hpp"

#include "log_parsers.hpp"

#include <windows.h>
#include <shlobj.h>

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <cwchar>
#include <cwctype>
#include <deque>
#include <map>
#include <iomanip>
#include <cctype>
#include <cmath>
#include <sstream>
#include <string>
#include <vector>

namespace helper::logs
{
    namespace
    {
        std::uint64_t to_ms(const std::chrono::system_clock::time_point& tp)
        {
            if (tp.time_since_epoch().count() == 0)
            {
                return 0;
            }
            return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(tp.time_since_epoch()).count());
        }

        bool starts_with_case_insensitive(const std::wstring& value, const std::wstring& prefix)
        {
            if (value.size() < prefix.size())
            {
                return false;
            }

            for (std::size_t i = 0; i < prefix.size(); ++i)
            {
                if (std::towlower(value[i]) != std::towlower(prefix[i]))
                {
                    return false;
                }
            }
            return true;
        }

        std::wstring to_wstring(const std::filesystem::path& path)
        {
            return path.native();
        }

        std::string format_time_utc(const std::chrono::system_clock::time_point& tp)
        {
            const auto seconds = std::chrono::system_clock::to_time_t(tp);
            std::tm tm{};
            gmtime_s(&tm, &seconds);
            std::ostringstream oss;
            oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S UTC");
            return oss.str();
        }

        std::string sanitize(std::string value)
        {
            value.erase(std::remove(value.begin(), value.end(), '\r'), value.end());
            value.erase(std::remove(value.begin(), value.end(), '\n'), value.end());
            return value;
        }

        std::string make_bucket_id(const std::string& label)
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
    }

    LogWatcher::LogWatcher(Config config, const SystemResolver& resolver, PublishCallback publishCallback, StatusCallback statusCallback, FollowModeSupplier followSupplier)
        : config_(std::move(config))
        , resolver_(resolver)
        , publishCallback_(std::move(publishCallback))
        , statusCallback_(std::move(statusCallback))
        , combatTelemetryAggregator_(std::make_unique<CombatTelemetryAggregator>())
    , miningTelemetryAggregator_(std::make_unique<MiningTelemetryAggregator>())
    , telemetryHistoryAggregator_(std::make_unique<TelemetryHistoryAggregator>())
    , followModeSupplier_(std::move(followSupplier))
    {
    }

    class LogWatcher::CombatTelemetryAggregator
    {
    public:
        void add(const CombatDamageEvent& event)
        {
            prune(event.timestamp);
            recent_.push_back(event);
            
            // Track session start on first event
            if (sessionStart_.time_since_epoch().count() == 0 && event.timestamp.time_since_epoch().count() != 0)
            {
                sessionStart_ = event.timestamp;
            }
            
            if (event.playerDealt)
            {
                totalDamageDealt_ += event.amount;
                
                // Increment hit quality counter (dealt)
                switch (event.quality)
                {
                    case HitQuality::Miss:        ++missDealt_; break;
                    case HitQuality::Glancing:    ++glancingDealt_; break;
                    case HitQuality::Standard:    ++standardDealt_; break;
                    case HitQuality::Penetrating: ++penetratingDealt_; break;
                    case HitQuality::Smashing:    ++smashingDealt_; break;
                }
            }
            else
            {
                totalDamageTaken_ += event.amount;
                
                // Increment hit quality counter (taken)
                switch (event.quality)
                {
                    case HitQuality::Miss:        ++missTaken_; break;
                    case HitQuality::Glancing:    ++glancingTaken_; break;
                    case HitQuality::Standard:    ++standardTaken_; break;
                    case HitQuality::Penetrating: ++penetratingTaken_; break;
                    case HitQuality::Smashing:    ++smashingTaken_; break;
                }
            }
            lastEvent_ = event.timestamp;
        }

        std::optional<CombatTelemetrySnapshot> snapshot(const std::chrono::system_clock::time_point& now)
        {
            prune(now);

            if (recent_.empty() && totalDamageDealt_ == 0.0 && totalDamageTaken_ == 0.0)
            {
                return std::nullopt;
            }

            CombatTelemetrySnapshot snapshot;
            snapshot.totalDamageDealt = totalDamageDealt_;
            snapshot.totalDamageTaken = totalDamageTaken_;
            snapshot.recentWindowSeconds = static_cast<double>(window_.count());
            
            // Copy hit quality counters (dealt)
            snapshot.missDealt = missDealt_;
            snapshot.glancingDealt = glancingDealt_;
            snapshot.standardDealt = standardDealt_;
            snapshot.penetratingDealt = penetratingDealt_;
            snapshot.smashingDealt = smashingDealt_;
            
            // Copy hit quality counters (taken)
            snapshot.missTaken = missTaken_;
            snapshot.glancingTaken = glancingTaken_;
            snapshot.standardTaken = standardTaken_;
            snapshot.penetratingTaken = penetratingTaken_;
            snapshot.smashingTaken = smashingTaken_;
            
            if (sessionStart_.time_since_epoch().count() != 0)
            {
                snapshot.sessionStartMs = to_ms(sessionStart_);
                
                // Calculate session duration
                if (now >= sessionStart_)
                {
                    const auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(now - sessionStart_);
                    snapshot.sessionDurationSeconds = static_cast<double>(duration.count()) / 1000.0;
                }
            }
            
            if (lastEvent_.time_since_epoch().count() != 0)
            {
                snapshot.lastEventMs = to_ms(lastEvent_);
            }

            double recentDealt = 0.0;
            double recentTaken = 0.0;
            for (const auto& ev : recent_)
            {
                if (ev.playerDealt)
                {
                    recentDealt += ev.amount;
                }
                else
                {
                    recentTaken += ev.amount;
                }
            }
            snapshot.recentDamageDealt = recentDealt;
            snapshot.recentDamageTaken = recentTaken;

            if (!snapshot.hasData() && snapshot.recentDamageDealt <= 0.0 && snapshot.recentDamageTaken <= 0.0)
            {
                return std::nullopt;
            }

            return snapshot;
        }

        void reset()
        {
            recent_.clear();
            totalDamageDealt_ = 0.0;
            totalDamageTaken_ = 0.0;
            lastEvent_ = std::chrono::system_clock::time_point{};
            sessionStart_ = std::chrono::system_clock::time_point{};
            
            // Clear hit quality counters (dealt)
            missDealt_ = 0;
            glancingDealt_ = 0;
            standardDealt_ = 0;
            penetratingDealt_ = 0;
            smashingDealt_ = 0;
            
            // Clear hit quality counters (taken)
            missTaken_ = 0;
            glancingTaken_ = 0;
            standardTaken_ = 0;
            penetratingTaken_ = 0;
            smashingTaken_ = 0;
        }

        void restoreSession(const CombatTelemetrySnapshot& persisted)
        {
            totalDamageDealt_ = persisted.totalDamageDealt;
            totalDamageTaken_ = persisted.totalDamageTaken;
            
            // Restore hit quality counters (dealt)
            missDealt_ = persisted.missDealt;
            glancingDealt_ = persisted.glancingDealt;
            standardDealt_ = persisted.standardDealt;
            penetratingDealt_ = persisted.penetratingDealt;
            smashingDealt_ = persisted.smashingDealt;
            
            // Restore hit quality counters (taken)
            missTaken_ = persisted.missTaken;
            glancingTaken_ = persisted.glancingTaken;
            standardTaken_ = persisted.standardTaken;
            penetratingTaken_ = persisted.penetratingTaken;
            smashingTaken_ = persisted.smashingTaken;
            
            if (persisted.sessionStartMs > 0)
            {
                sessionStart_ = std::chrono::system_clock::time_point{std::chrono::milliseconds(persisted.sessionStartMs)};
            }
            
            if (persisted.lastEventMs > 0)
            {
                lastEvent_ = std::chrono::system_clock::time_point{std::chrono::milliseconds(persisted.lastEventMs)};
            }
            
            spdlog::info("Restored combat session: {:.1f} dealt, {:.1f} taken", 
                         totalDamageDealt_, totalDamageTaken_);
        }

    private:
        void prune(const std::chrono::system_clock::time_point& now)
        {
            const auto cutoff = now - window_;
            while (!recent_.empty() && recent_.front().timestamp < cutoff)
            {
                recent_.pop_front();
            }
        }

        std::deque<CombatDamageEvent> recent_;
        double totalDamageDealt_{0.0};
        double totalDamageTaken_{0.0};
        std::chrono::seconds window_{30};
        std::chrono::system_clock::time_point lastEvent_{};
        std::chrono::system_clock::time_point sessionStart_{};
        
        // Hit quality counters (dealt)
        std::uint64_t missDealt_{0};
        std::uint64_t glancingDealt_{0};
        std::uint64_t standardDealt_{0};
        std::uint64_t penetratingDealt_{0};
        std::uint64_t smashingDealt_{0};
        
        // Hit quality counters (taken)
        std::uint64_t missTaken_{0};
        std::uint64_t glancingTaken_{0};
        std::uint64_t standardTaken_{0};
        std::uint64_t penetratingTaken_{0};
        std::uint64_t smashingTaken_{0};
    };

    class LogWatcher::MiningTelemetryAggregator
    {
    public:
        static constexpr std::chrono::seconds kDefaultWindow{120};

        void add(const MiningYieldEvent& event)
        {
            MiningYieldEvent normalized = event;
            normalized.resource = normalizeResourceLabel(normalized.resource);
            if (normalized.resource.empty())
            {
                normalized.resource = "Unknown resource";
            }

            prune(normalized.timestamp);

            // Only set sessionStart_ if this is the FIRST event ever (not after restore)
            // After restore, sessionStart_ is already set to the original session start time
            if (sessionStart_.time_since_epoch().count() == 0 && normalized.timestamp.time_since_epoch().count() != 0)
            {
                sessionStart_ = normalized.timestamp;
            }

            recent_.push_back(std::move(normalized));
            const auto& stored = recent_.back();
            totalVolume_ += stored.volumeM3;
            lastEvent_ = stored.timestamp;
            sessionBuckets_[stored.resource] += stored.volumeM3;
        }

        std::optional<MiningTelemetrySnapshot> snapshot(const std::chrono::system_clock::time_point& now)
        {
            prune(now);

            if (recent_.empty() && totalVolume_ == 0.0)
            {
                return std::nullopt;
            }

            MiningTelemetrySnapshot snapshot;
            snapshot.totalVolumeM3 = totalVolume_;
            snapshot.recentWindowSeconds = static_cast<double>(window_.count());
            if (lastEvent_.time_since_epoch().count() != 0)
            {
                snapshot.lastEventMs = to_ms(lastEvent_);
            }
            if (sessionStart_.time_since_epoch().count() != 0)
            {
                snapshot.sessionStartMs = to_ms(sessionStart_);
                const auto elapsed = now - sessionStart_;
                snapshot.sessionDurationSeconds = std::chrono::duration_cast<std::chrono::duration<double>>(elapsed).count();
            }

            double recentVolume = 0.0;
            std::map<std::string, double> recentBuckets;
            for (const auto& ev : recent_)
            {
                recentVolume += ev.volumeM3;
                recentBuckets[ev.resource] += ev.volumeM3;
            }
            snapshot.recentVolumeM3 = recentVolume;

            if (!sessionBuckets_.empty() || !recentBuckets.empty())
            {
                snapshot.buckets.reserve(sessionBuckets_.size() + recentBuckets.size());

                for (const auto& kv : sessionBuckets_)
                {
                    MiningBucketSnapshot bucket;
                    bucket.resource = kv.first;
                    bucket.sessionTotalM3 = kv.second;
                    if (auto it = recentBuckets.find(kv.first); it != recentBuckets.end())
                    {
                        bucket.recentVolumeM3 = it->second;
                    }
                    snapshot.buckets.push_back(std::move(bucket));
                }

                for (const auto& kv : recentBuckets)
                {
                    if (sessionBuckets_.find(kv.first) != sessionBuckets_.end())
                    {
                        continue;
                    }
                    MiningBucketSnapshot bucket;
                    bucket.resource = kv.first;
                    bucket.sessionTotalM3 = 0.0;
                    bucket.recentVolumeM3 = kv.second;
                    snapshot.buckets.push_back(std::move(bucket));
                }

                std::sort(snapshot.buckets.begin(), snapshot.buckets.end(), [](const MiningBucketSnapshot& a, const MiningBucketSnapshot& b) {
                    constexpr double epsilon = 1e-6;
                    const double diffSession = a.sessionTotalM3 - b.sessionTotalM3;
                    if (std::abs(diffSession) > epsilon)
                    {
                        return diffSession > 0.0;
                    }
                    const double diffRecent = a.recentVolumeM3 - b.recentVolumeM3;
                    if (std::abs(diffRecent) > epsilon)
                    {
                        return diffRecent > 0.0;
                    }
                    return a.resource < b.resource;
                });
            }

            if (!snapshot.hasData() && snapshot.recentVolumeM3 <= 0.0)
            {
                return std::nullopt;
            }

            return snapshot;
        }

        void reset()
        {
            recent_.clear();
            totalVolume_ = 0.0;
            lastEvent_ = std::chrono::system_clock::time_point{};
            sessionBuckets_.clear();
            sessionStart_ = std::chrono::system_clock::time_point{};
        }

        void restoreSession(const MiningTelemetrySnapshot& persisted)
        {
            // Restore session state from persisted snapshot
            totalVolume_ = persisted.totalVolumeM3;
            
            if (persisted.sessionStartMs > 0)
            {
                sessionStart_ = std::chrono::system_clock::time_point{std::chrono::milliseconds(persisted.sessionStartMs)};
            }
            
            if (persisted.lastEventMs > 0)
            {
                lastEvent_ = std::chrono::system_clock::time_point{std::chrono::milliseconds(persisted.lastEventMs)};
            }
            
            // Restore bucket totals
            sessionBuckets_.clear();
            for (const auto& bucket : persisted.buckets)
            {
                if (bucket.sessionTotalM3 > 0.0)
                {
                    sessionBuckets_[bucket.resource] = bucket.sessionTotalM3;
                }
            }
            
            spdlog::info("Restored mining session: {:.1f} m³ total, {} ore types", 
                         totalVolume_, sessionBuckets_.size());
        }

    private:
        static std::string normalizeResourceLabel(std::string label)
        {
            const auto notSpace = [](unsigned char ch) {
                return std::isspace(ch) == 0;
            };
            auto begin = std::find_if(label.begin(), label.end(), notSpace);
            auto end = std::find_if(label.rbegin(), label.rend(), notSpace).base();
            if (begin >= end)
            {
                return {};
            }
            std::string trimmed(begin, end);
            return trimmed;
        }

        void prune(const std::chrono::system_clock::time_point& now)
        {
            const auto cutoff = now - window_;
            while (!recent_.empty() && recent_.front().timestamp < cutoff)
            {
                recent_.pop_front();
            }
        }

        std::deque<MiningYieldEvent> recent_;
        double totalVolume_{0.0};
        std::chrono::seconds window_{kDefaultWindow};
        std::chrono::system_clock::time_point lastEvent_{};
        std::map<std::string, double> sessionBuckets_;
        std::chrono::system_clock::time_point sessionStart_{};
    };

    class LogWatcher::TelemetryHistoryAggregator
    {
    public:
        TelemetryHistoryAggregator()
        {
            const auto historySeconds = historyDuration_.count();
            const auto sliceSeconds = sliceDuration_.count();
            if (sliceSeconds > 0)
            {
                capacity_ = static_cast<std::size_t>(historySeconds / sliceSeconds);
                if (capacity_ == 0)
                {
                    capacity_ = 1;
                }
            }
        }

        void addCombat(const CombatDamageEvent& event)
        {
            if (event.timestamp.time_since_epoch().count() == 0)
            {
                return;
            }

            double dealt = event.playerDealt ? event.amount : 0.0;
            double taken = event.playerDealt ? 0.0 : event.amount;
            record(event.timestamp, dealt, taken, 0.0);
        }

        void addMining(const MiningYieldEvent& event)
        {
            if (event.timestamp.time_since_epoch().count() == 0)
            {
                return;
            }

            record(event.timestamp, 0.0, 0.0, event.volumeM3);
        }

        void resetSession(const std::chrono::system_clock::time_point& now)
        {
            const auto marker = to_ms(now);
            if (marker == 0)
            {
                return;
            }
            resetMarkers_.push_back(marker);
            pruneMarkers(cutoffMs(marker));
        }

        void resetAll()
        {
            slices_.clear();
            resetMarkers_.clear();
            saturated_ = false;
        }

        TelemetryHistorySnapshot snapshot(const std::chrono::system_clock::time_point& now)
        {
            prune(now);

            TelemetryHistorySnapshot snapshot;
            snapshot.sliceSeconds = static_cast<double>(sliceDuration_.count());
            snapshot.capacity = static_cast<std::uint32_t>(capacity_);
            snapshot.saturated = saturated_;
            snapshot.resetMarkersMs = resetMarkers_;

            const auto cutoff = cutoffMs(to_ms(now));
            snapshot.slices.reserve(slices_.size());
            for (const auto& entry : slices_)
            {
                if (entry.first < cutoff)
                {
                    continue;
                }
                TelemetryHistorySliceSnapshot slice;
                slice.startMs = entry.first;
                slice.durationSeconds = static_cast<double>(sliceDuration_.count());
                slice.damageDealt = entry.second.damageDealt;
                slice.damageTaken = entry.second.damageTaken;
                slice.miningVolumeM3 = entry.second.miningVolume;
                snapshot.slices.push_back(std::move(slice));
            }

            return snapshot;
        }

    private:
        struct Slice
        {
            double damageDealt{0.0};
            double damageTaken{0.0};
            double miningVolume{0.0};
        };

        void record(const std::chrono::system_clock::time_point& timestamp, double dealt, double taken, double mining)
        {
            const auto ms = to_ms(timestamp);
            if (ms == 0)
            {
                return;
            }

            const auto start = alignToSlice(ms);
            auto& slice = slices_[start];
            slice.damageDealt += dealt;
            slice.damageTaken += taken;
            slice.miningVolume += mining;

            prune(std::chrono::system_clock::time_point{std::chrono::milliseconds{ms}});
        }

        void prune(const std::chrono::system_clock::time_point& now)
        {
            const auto cutoff = cutoffMs(to_ms(now));
            if (cutoff > 0)
            {
                while (!slices_.empty())
                {
                    auto it = slices_.begin();
                    if (it->first >= cutoff)
                    {
                        break;
                    }
                    slices_.erase(it);
                }

                pruneMarkers(cutoff);
            }

            if (capacity_ > 0 && slices_.size() > capacity_)
            {
                saturated_ = true;
                while (slices_.size() > capacity_)
                {
                    slices_.erase(slices_.begin());
                }
            }
        }

        std::uint64_t alignToSlice(std::uint64_t ms) const
        {
            const auto sliceMs = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(sliceDuration_).count());
            if (sliceMs == 0)
            {
                return ms;
            }
            return (ms / sliceMs) * sliceMs;
        }

        std::uint64_t cutoffMs(std::uint64_t reference) const
        {
            const auto historyMs = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(historyDuration_).count());
            if (historyMs == 0 || reference < historyMs)
            {
                return 0;
            }
            return reference - historyMs;
        }

        void pruneMarkers(std::uint64_t cutoff)
        {
            if (cutoff == 0)
            {
                return;
            }
            auto it = resetMarkers_.begin();
            while (it != resetMarkers_.end())
            {
                if (*it >= cutoff)
                {
                    break;
                }
                it = resetMarkers_.erase(it);
            }
        }

        std::map<std::uint64_t, Slice> slices_;
        std::vector<std::uint64_t> resetMarkers_;
        std::chrono::seconds sliceDuration_{std::chrono::minutes(5)};
        std::chrono::seconds historyDuration_{std::chrono::hours(24)};
        std::size_t capacity_{0};
        bool saturated_{false};
    };

    LogWatcher::~LogWatcher()
    {
        stop();
    }

    void LogWatcher::start()
    {
        std::lock_guard<std::mutex> guard(mutex_);
        if (running_.load())
        {
            return;
        }

        stopRequested_.store(false);
        running_.store(true);
        worker_ = std::thread([this]() {
            run();
        });
    }

    void LogWatcher::stop()
    {
        {
            std::lock_guard<std::mutex> guard(mutex_);
            if (!running_.load())
            {
                return;
            }
            stopRequested_.store(true);
        }

        cv_.notify_all();
        if (worker_.joinable())
        {
            worker_.join();
        }
        running_.store(false);
    }

    LogWatcherStatus LogWatcher::status() const
    {
        std::lock_guard<std::mutex> guard(mutex_);
        return status_;
    }

    void LogWatcher::run()
    {
        spdlog::info("Log watcher thread starting");

        const auto pollInterval = std::max(std::chrono::milliseconds{250}, config_.pollInterval);

        while (!stopRequested_.load())
        {
            LogWatcherStatus snapshot;
            bool publish = false;
            bool forcePublish = false;

            {
                std::unique_lock<std::mutex> lock(mutex_);
                status_.running = true;

                discoverDirectories();
                forcePublish |= refreshChatFile();
                refreshCombatFile();

                publish |= processLocalChat();
                publish |= processCombat();

                snapshot = status_;
                
                // Debug logging to track mining data persistence
                if (snapshot.telemetry.mining.has_value())
                {
                    spdlog::debug("Loop snapshot has mining data: {:.1f} m3", snapshot.telemetry.mining->totalVolumeM3);
                }
                else
                {
                    spdlog::warn("Loop snapshot MISSING mining data!");
                }
            }

            if (statusCallback_)
            {
                statusCallback_(snapshot);
            }

            publishStateIfNeeded(snapshot, publish || forcePublish);

            std::unique_lock<std::mutex> waitLock(mutex_);
            cv_.wait_for(waitLock, pollInterval, [this]() {
                return stopRequested_.load();
            });
        }

        spdlog::info("Log watcher thread stopping");
    }

    bool LogWatcher::discoverDirectories()
    {
        bool changed = false;

        if (config_.chatDirectoryOverride.has_value())
        {
            const auto desired = *config_.chatDirectoryOverride;
            if (status_.chatDirectory != desired)
            {
                status_.chatDirectory = desired;
                chatTail_.reset({});
                changed = true;
            }
        }
        else if (status_.chatDirectory.empty())
        {
            if (auto resolved = resolveDefaultDirectory(L"Chatlogs"))
            {
                status_.chatDirectory = *resolved;
                chatTail_.reset({});
                changed = true;
            }
        }

        if (config_.combatDirectoryOverride.has_value())
        {
            const auto desired = *config_.combatDirectoryOverride;
            if (status_.combatDirectory != desired)
            {
                status_.combatDirectory = desired;
                combatTail_.reset({});
            }
        }
        else if (status_.combatDirectory.empty())
        {
            if (auto resolved = resolveDefaultDirectory(L"Gamelogs"))
            {
                status_.combatDirectory = *resolved;
                combatTail_.reset({});
            }
        }

        if (!status_.chatDirectory.empty() && !status_.combatDirectory.empty())
        {
            status_.lastError.clear();
        }
        else if (status_.lastError.empty())
        {
            status_.lastError = "Waiting for Frontier log directories";
        }

        return changed;
    }

    bool LogWatcher::refreshChatFile()
    {
        if (status_.chatDirectory.empty())
        {
            chatTail_.path.clear();
            return false;
        }

        auto latest = latestChatLogPath(status_.chatDirectory);
        if (!latest.has_value())
        {
            chatTail_.path.clear();
            status_.chatFile.clear();
            return false;
        }

        if (chatTail_.path != *latest)
        {
            chatTail_.reset(*latest);
            status_.chatFile = *latest;
            lastPublishedSystemId_.reset();
            status_.lastError.clear();
            return true;
        }

        std::error_code ec;
        const auto writeTime = std::filesystem::last_write_time(*latest, ec);
        if (!ec)
        {
            chatWriteTime_ = writeTime;
        }

        return false;
    }

    bool LogWatcher::refreshCombatFile()
    {
        if (status_.combatDirectory.empty())
        {
            combatTail_.path.clear();
            return false;
        }

        auto latest = latestCombatLogPath(status_.combatDirectory);
        if (!latest.has_value())
        {
            combatTail_.path.clear();
            status_.combatFile.clear();
            status_.combat.reset();
            return false;
        }

        if (combatTail_.path != *latest)
        {
            // Preserve mining session data when switching combat log files
            // Save both the snapshot and restore aggregator state to maintain session continuity
            auto preservedMining = status_.telemetry.mining;
            
            combatTail_.reset(*latest);
            status_.combatFile = *latest;
            status_.combat.emplace();
            combatTelemetryAggregator_->reset();
            miningTelemetryAggregator_->reset();
            telemetryHistoryAggregator_->resetAll();
            status_.telemetry = TelemetrySummary{};
            
            // Restore mining session if it was set (from restoreMiningSession or previous state)
            status_.telemetry.mining = preservedMining;
            
            // Also restore the aggregator's internal state so new events accumulate correctly
            if (preservedMining.has_value())
            {
                miningTelemetryAggregator_->restoreSession(*preservedMining);
            }
            
            if (auto id = combat_log_character_id(latest->filename().string()))
            {
                status_.combat->characterId = *id;
            }
            else
            {
                status_.combat->characterId.clear();
            }
        }

        std::error_code ec;
        const auto writeTime = std::filesystem::last_write_time(*latest, ec);
        if (!ec)
        {
            combatWriteTime_ = writeTime;
        }

        return false;
    }

    bool LogWatcher::processLocalChat()
    {
        if (chatTail_.path.empty())
        {
            return false;
        }

        auto lines = readNewLines(chatTail_);
        bool updated = false;
        for (const auto& line : lines)
        {
            auto parsed = parse_local_chat_line(line);
            if (!parsed.has_value())
            {
                continue;
            }

            LocationSample sample;
            sample.systemName = parsed->systemName;
            sample.systemId = parsed->systemName;
            sample.observedAt = std::chrono::system_clock::now();

            if (const auto resolved = resolver_.resolve(sample.systemName))
            {
                sample.systemId = *resolved;
                status_.lastError.clear();
            }
            else
            {
                status_.lastError = "Unmapped system name: " + sample.systemName;
                spdlog::warn("LogWatcher unable to resolve system name '{}'", sample.systemName);
            }

            status_.location = std::move(sample);
            updated = true;
        }

        return updated;
    }

    bool LogWatcher::processCombat()
    {
        if (combatTail_.path.empty())
        {
            return false;
        }

        auto lines = readNewLines(combatTail_);
        if (lines.empty())
        {
            return false;
        }

        if (!status_.combat.has_value())
        {
            status_.combat.emplace();
            if (auto id = combat_log_character_id(combatTail_.path.filename().string()))
            {
                status_.combat->characterId = *id;
            }
        }

        bool updated = false;
        bool telemetryUpdated = false;
        for (const auto& line : lines)
        {
            if (const auto combatEvent = parse_combat_damage_line(line))
            {
                combatTelemetryAggregator_->add(*combatEvent);
                telemetryHistoryAggregator_->addCombat(*combatEvent);
                telemetryUpdated = true;
            }

            if (const auto miningEvent = parse_mining_yield_line(line))
            {
                miningTelemetryAggregator_->add(*miningEvent);
                telemetryHistoryAggregator_->addMining(*miningEvent);
                telemetryUpdated = true;
            }

            if (line.find("(combat)") != std::string::npos)
            {
                ++status_.combat->combatEventCount;
                status_.combat->lastCombatLine = sanitize(line);
                status_.combat->lastEventAt = std::chrono::system_clock::now();
                updated = true;
            }
            else if (line.find("(notify)") != std::string::npos)
            {
                ++status_.combat->notifyEventCount;
                status_.combat->lastEventAt = std::chrono::system_clock::now();
                updated = true;
            }
        }

        if (telemetryUpdated)
        {
            const auto now = std::chrono::system_clock::now();
            status_.telemetry.combat = combatTelemetryAggregator_->snapshot(now);
            status_.telemetry.mining = miningTelemetryAggregator_->snapshot(now);
            auto historySnapshot = telemetryHistoryAggregator_->snapshot(now);
            if (historySnapshot.hasData() || !historySnapshot.resetMarkersMs.empty())
            {
                status_.telemetry.history = std::move(historySnapshot);
            }
            else
            {
                status_.telemetry.history.reset();
            }
        }

        return updated || telemetryUpdated;
    }

    std::vector<std::string> LogWatcher::readNewLines(FileTailState& state)
    {
        std::vector<std::string> lines;
        if (state.path.empty())
        {
            return lines;
        }

        const auto pathW = to_wstring(state.path);
        HANDLE handle = CreateFileW(pathW.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (handle == INVALID_HANDLE_VALUE)
        {
            status_.lastError = "Unable to open log file";
            return lines;
        }

        LARGE_INTEGER size{};
        if (!GetFileSizeEx(handle, &size))
        {
            CloseHandle(handle);
            return lines;
        }

        std::uint64_t fileSize = static_cast<std::uint64_t>(size.QuadPart);
        if (fileSize < state.offset)
        {
            state.offset = 0;
            state.encoding = TextEncoding::Unknown;
            state.consumedBom = false;
            state.pendingLine.clear();
            state.pendingBytes.clear();
        }

        if (fileSize == state.offset)
        {
            CloseHandle(handle);
            return lines;
        }

        std::uint64_t remaining = fileSize - state.offset;
        std::vector<char> buffer(static_cast<std::size_t>(remaining));

        LARGE_INTEGER seek{};
        seek.QuadPart = static_cast<LONGLONG>(state.offset);
        if (!SetFilePointerEx(handle, seek, nullptr, FILE_BEGIN))
        {
            CloseHandle(handle);
            return lines;
        }

        DWORD totalRead = 0;
        while (remaining > 0)
        {
            const DWORD chunk = static_cast<DWORD>(std::min<std::uint64_t>(remaining, static_cast<std::uint64_t>(64 * 1024)));
            DWORD read = 0;
            if (!ReadFile(handle, buffer.data() + totalRead, chunk, &read, nullptr) || read == 0)
            {
                break;
            }
            totalRead += read;
            remaining -= read;
        }
        CloseHandle(handle);

        if (totalRead == 0)
        {
            return lines;
        }

        buffer.resize(totalRead);
        state.offset += totalRead;

        if (!state.pendingBytes.empty())
        {
            buffer.insert(buffer.begin(), state.pendingBytes.begin(), state.pendingBytes.end());
            state.pendingBytes.clear();
        }

        const bool firstChunk = !state.consumedBom;
        auto converted = convertToUtf8(state, buffer, firstChunk);
        if (converted.empty())
        {
            return lines;
        }

        state.consumedBom = true;

        if (converted.size() >= 3 && static_cast<unsigned char>(converted[0]) == 0xEF && static_cast<unsigned char>(converted[1]) == 0xBB && static_cast<unsigned char>(converted[2]) == 0xBF)
        {
            converted.erase(0, 3);
        }

        std::string combined;
        combined.reserve(state.pendingLine.size() + converted.size());
        combined.append(state.pendingLine);
        combined.append(converted);
        state.pendingLine.clear();

        std::size_t position = 0;
        while (position < combined.size())
        {
            const auto newline = combined.find_first_of("\r\n", position);
            if (newline == std::string::npos)
            {
                break;
            }

            std::size_t next = newline + 1;
            if (combined[newline] == '\r' && next < combined.size() && combined[next] == '\n')
            {
                ++next;
            }

            lines.emplace_back(combined.substr(position, newline - position));
            position = next;
        }

        state.pendingLine = combined.substr(position);
        return lines;
    }

    bool LogWatcher::ensureUtf16Even(FileTailState& state, std::vector<char>& buffer)
    {
        if (buffer.empty())
        {
            return false;
        }

        if (buffer.size() % 2 == 0)
        {
            return true;
        }

        state.pendingBytes.push_back(buffer.back());
        buffer.pop_back();
        return !buffer.empty();
    }

    std::string LogWatcher::convertToUtf8(FileTailState& state, std::vector<char>& buffer, bool isFirstChunk)
    {
        if (buffer.empty())
        {
            return {};
        }

        if (state.encoding == TextEncoding::Unknown)
        {
            if (buffer.size() >= 2 && static_cast<unsigned char>(buffer[0]) == 0xFF && static_cast<unsigned char>(buffer[1]) == 0xFE)
            {
                state.encoding = TextEncoding::Utf16LE;
            }
            else
            {
                state.encoding = TextEncoding::Utf8;
            }
        }

        if (state.encoding == TextEncoding::Utf8)
        {
            std::size_t start = 0;
            if (isFirstChunk && buffer.size() >= 3 && static_cast<unsigned char>(buffer[0]) == 0xEF && static_cast<unsigned char>(buffer[1]) == 0xBB && static_cast<unsigned char>(buffer[2]) == 0xBF)
            {
                start = 3;
            }
            return std::string(buffer.data() + start, buffer.data() + buffer.size());
        }

        if (state.encoding == TextEncoding::Utf16LE)
        {
            std::size_t offset = 0;
            if (isFirstChunk && buffer.size() >= 2 && static_cast<unsigned char>(buffer[0]) == 0xFF && static_cast<unsigned char>(buffer[1]) == 0xFE)
            {
                offset = 2;
            }

            if (buffer.size() <= offset)
            {
                return {};
            }

            std::vector<char> slice(buffer.begin() + static_cast<std::ptrdiff_t>(offset), buffer.end());
            if (!ensureUtf16Even(state, slice))
            {
                return {};
            }

            const wchar_t* wide = reinterpret_cast<const wchar_t*>(slice.data());
            const int wcharCount = static_cast<int>(slice.size() / sizeof(wchar_t));
            if (wcharCount <= 0)
            {
                return {};
            }

            const int required = WideCharToMultiByte(CP_UTF8, 0, wide, wcharCount, nullptr, 0, nullptr, nullptr);
            if (required <= 0)
            {
                return {};
            }

            std::string utf8(static_cast<std::size_t>(required), '\0');
            WideCharToMultiByte(CP_UTF8, 0, wide, wcharCount, utf8.data(), required, nullptr, nullptr);
            return utf8;
        }

        return {};
    }

    std::optional<std::filesystem::path> LogWatcher::resolveDefaultDirectory(const wchar_t* subFolder) const
    {
        PWSTR rawPath = nullptr;
        std::filesystem::path documents;

        if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Documents, KF_FLAG_DEFAULT, nullptr, &rawPath)) && rawPath != nullptr)
        {
            documents = std::filesystem::path(rawPath);
            CoTaskMemFree(rawPath);
        }

        if (documents.empty())
        {
            wchar_t* userProfile = nullptr;
            std::size_t length = 0;
            if (_wdupenv_s(&userProfile, &length, L"USERPROFILE") == 0 && userProfile != nullptr)
            {
                documents = std::filesystem::path(userProfile) / L"Documents";
                std::free(userProfile);
            }
        }

        if (documents.empty())
        {
            return std::nullopt;
        }

        documents /= L"Frontier";
        documents /= L"Logs";
        documents /= subFolder;

        if (!std::filesystem::exists(documents))
        {
            return std::nullopt;
        }

        return documents;
    }

    std::optional<std::filesystem::path> LogWatcher::latestChatLogPath(const std::filesystem::path& directory) const
    {
        std::optional<std::filesystem::path> best;
        std::filesystem::file_time_type bestTime{};

        std::error_code ec;
        for (const auto& entry : std::filesystem::directory_iterator(directory, ec))
        {
            if (!entry.is_regular_file())
            {
                continue;
            }

            const auto filename = entry.path().filename().wstring();
            if (!starts_with_case_insensitive(filename, L"Local_"))
            {
                continue;
            }
            if (!entry.path().has_extension() || entry.path().extension() != ".txt")
            {
                continue;
            }

            const auto writeTime = entry.last_write_time(ec);
            if (ec)
            {
                continue;
            }

            if (!best.has_value() || writeTime > bestTime)
            {
                best = entry.path();
                bestTime = writeTime;
            }
        }

        return best;
    }

    std::optional<std::filesystem::path> LogWatcher::latestCombatLogPath(const std::filesystem::path& directory) const
    {
        std::optional<std::filesystem::path> best;
        std::filesystem::file_time_type bestTime{};

        std::error_code ec;
        for (const auto& entry : std::filesystem::directory_iterator(directory, ec))
        {
            if (!entry.is_regular_file())
            {
                continue;
            }

            const auto filename = entry.path().filename().string();
            if (!is_combat_log_filename(filename))
            {
                continue;
            }

            const auto writeTime = entry.last_write_time(ec);
            if (ec)
            {
                continue;
            }

            if (!best.has_value() || writeTime > bestTime)
            {
                best = entry.path();
                bestTime = writeTime;
            }
        }

        return best;
    }

    void LogWatcher::publishStateIfNeeded(const LogWatcherStatus& snapshot, bool forcePublish)
    {
        if (!publishCallback_)
        {
            return;
        }

        if (!snapshot.location.has_value())
        {
            if (!forcePublish)
            {
                return;
            }
        }

        const auto now = std::chrono::system_clock::now();
        bool shouldPublish = forcePublish;

        if (snapshot.location.has_value())
        {
            if (!lastPublishedSystemId_.has_value() || *lastPublishedSystemId_ != snapshot.location->systemId)
            {
                shouldPublish = true;
                lastPublishedSystemId_ = snapshot.location->systemId;
            }
        }

        if (!shouldPublish)
        {
            if (lastPublishedAt_.time_since_epoch().count() == 0 || (now - lastPublishedAt_) > std::chrono::seconds(30))
            {
                shouldPublish = true;
            }
        }

        if (!shouldPublish)
        {
            return;
        }

        const auto state = buildOverlayState(snapshot);
        const auto payload = overlay::serialize_overlay_state(state).dump();
        publishCallback_(state, payload.size());
        lastPublishedAt_ = now;
    }

    overlay::OverlayState LogWatcher::buildOverlayState(const LogWatcherStatus& snapshot) const
    {
        overlay::OverlayState state;
        state.generated_at_ms = now_ms();
        state.heartbeat_ms = state.generated_at_ms;
        state.follow_mode_enabled = followModeEnabled();
        state.source_online = true;

        if (snapshot.location.has_value())
        {
            overlay::RouteNode node;
            node.system_id = snapshot.location->systemId;
            node.display_name = snapshot.location->systemName;
            node.distance_ly = 0.0;
            node.via_gate = false;
            state.route.push_back(std::move(node));

            overlay::PlayerMarker marker;
            marker.system_id = snapshot.location->systemId;
            marker.display_name = snapshot.location->systemName;
            marker.is_docked = false;
            state.player_marker = marker;
        }
        else
        {
            overlay::RouteNode node;
            node.system_id = "LOG-WATCH";
            node.display_name = "Awaiting log data";
            node.distance_ly = 0.0;
            node.via_gate = false;
            state.route.push_back(std::move(node));
            state.notes = std::string{"Log watcher active, waiting for Local chat entry."};
        }

        if (!state.notes.has_value())
        {
            state.notes = buildStatusNotes(snapshot);
        }

        if (snapshot.telemetry.combat.has_value() || snapshot.telemetry.mining.has_value())
        {
            overlay::TelemetryMetrics metrics;
            if (snapshot.telemetry.combat.has_value())
            {
                const auto& combat = *snapshot.telemetry.combat;
                if (combat.hasData())
                {
                    overlay::CombatTelemetry payload;
                    payload.total_damage_dealt = combat.totalDamageDealt;
                    payload.total_damage_taken = combat.totalDamageTaken;
                    payload.recent_damage_dealt = combat.recentDamageDealt;
                    payload.recent_damage_taken = combat.recentDamageTaken;
                    payload.recent_window_seconds = combat.recentWindowSeconds;
                    payload.last_event_ms = combat.lastEventMs;
                    payload.session_start_ms = combat.sessionStartMs;
                    payload.session_duration_seconds = combat.sessionDurationSeconds;
                    
                    // Hit quality counters (dealt)
                    payload.miss_dealt = combat.missDealt;
                    payload.glancing_dealt = combat.glancingDealt;
                    payload.standard_dealt = combat.standardDealt;
                    payload.penetrating_dealt = combat.penetratingDealt;
                    payload.smashing_dealt = combat.smashingDealt;
                    
                    // Hit quality counters (taken)
                    payload.miss_taken = combat.missTaken;
                    payload.glancing_taken = combat.glancingTaken;
                    payload.standard_taken = combat.standardTaken;
                    payload.penetrating_taken = combat.penetratingTaken;
                    payload.smashing_taken = combat.smashingTaken;
                    
                    metrics.combat = payload;
                }
            }

            if (snapshot.telemetry.mining.has_value())
            {
                const auto& mining = *snapshot.telemetry.mining;
                if (mining.hasData())
                {
                    overlay::MiningTelemetry payload;
                    payload.total_volume_m3 = mining.totalVolumeM3;
                    payload.recent_volume_m3 = mining.recentVolumeM3;
                    payload.recent_window_seconds = mining.recentWindowSeconds;
                    payload.last_event_ms = mining.lastEventMs;
                    payload.session_start_ms = mining.sessionStartMs;
                    payload.session_duration_seconds = mining.sessionDurationSeconds;
                    if (!mining.buckets.empty())
                    {
                        payload.buckets.reserve(mining.buckets.size());
                        for (const auto& bucket : mining.buckets)
                        {
                            overlay::TelemetryBucket schemaBucket;
                            schemaBucket.id = make_bucket_id(bucket.resource);
                            schemaBucket.label = bucket.resource;
                            schemaBucket.session_total = bucket.sessionTotalM3;
                            schemaBucket.recent_total = bucket.recentVolumeM3;
                            payload.buckets.push_back(std::move(schemaBucket));
                        }
                    }
                    metrics.mining = payload;
                }
            }

            if (snapshot.telemetry.history.has_value())
            {
                const auto& history = *snapshot.telemetry.history;
                overlay::TelemetryHistory historyPayload;
                historyPayload.slice_seconds = history.sliceSeconds;
                historyPayload.capacity = history.capacity;
                historyPayload.saturated = history.saturated;
                historyPayload.reset_markers_ms = history.resetMarkersMs;
                historyPayload.slices.reserve(history.slices.size());
                for (const auto& slice : history.slices)
                {
                    overlay::TelemetryHistorySlice payloadSlice;
                    payloadSlice.start_ms = slice.startMs;
                    payloadSlice.duration_seconds = slice.durationSeconds;
                    payloadSlice.damage_dealt = slice.damageDealt;
                    payloadSlice.damage_taken = slice.damageTaken;
                    payloadSlice.mining_volume_m3 = slice.miningVolumeM3;
                    historyPayload.slices.push_back(std::move(payloadSlice));
                }
                metrics.history = std::move(historyPayload);
            }

            if (metrics.combat.has_value() || metrics.mining.has_value() || metrics.history.has_value())
            {
                state.telemetry = std::move(metrics);
            }
        }

        return state;
    }

    void LogWatcher::setFollowModeSupplier(FollowModeSupplier supplier)
    {
        followModeSupplier_ = std::move(supplier);
    }

    bool LogWatcher::followModeEnabled() const
    {
        if (followModeSupplier_)
        {
            try
            {
                return followModeSupplier_();
            }
            catch (...)
            {
                return true;
            }
        }
        return true;
    }

    TelemetrySummary LogWatcher::telemetrySnapshot()
    {
        std::lock_guard<std::mutex> guard(mutex_);
        const auto now = std::chrono::system_clock::now();
        TelemetrySummary summary;
        summary.combat = combatTelemetryAggregator_->snapshot(now);
        summary.mining = miningTelemetryAggregator_->snapshot(now);
        if (!summary.mining.has_value())
        {
            MiningTelemetrySnapshot placeholder;
            placeholder.recentWindowSeconds = static_cast<double>(MiningTelemetryAggregator::kDefaultWindow.count());
            summary.mining = std::move(placeholder);
        }
        auto historySnapshot = telemetryHistoryAggregator_->snapshot(now);
        if (historySnapshot.hasData() || !historySnapshot.resetMarkersMs.empty())
        {
            summary.history = std::move(historySnapshot);
        }

        status_.telemetry = summary;
        return summary;
    }

    TelemetrySummary LogWatcher::resetTelemetrySession()
    {
        std::lock_guard<std::mutex> guard(mutex_);
        const auto now = std::chrono::system_clock::now();
        combatTelemetryAggregator_->reset();
        miningTelemetryAggregator_->reset();
        telemetryHistoryAggregator_->resetSession(now);

        TelemetrySummary summary;
        summary.combat = combatTelemetryAggregator_->snapshot(now);
        summary.mining = miningTelemetryAggregator_->snapshot(now);
        if (!summary.mining.has_value())
        {
            MiningTelemetrySnapshot placeholder;
            placeholder.recentWindowSeconds = static_cast<double>(MiningTelemetryAggregator::kDefaultWindow.count());
            summary.mining = std::move(placeholder);
        }
        auto historySnapshot = telemetryHistoryAggregator_->snapshot(now);
        if (historySnapshot.hasData() || !historySnapshot.resetMarkersMs.empty())
        {
            summary.history = std::move(historySnapshot);
        }

        status_.telemetry = summary;
        return summary;
    }

    void LogWatcher::restoreMiningSession(const MiningTelemetrySnapshot& persisted)
    {
        std::lock_guard<std::mutex> guard(mutex_);
        if (miningTelemetryAggregator_)
        {
            spdlog::info("LogWatcher::restoreMiningSession() - Restoring {:.1f} m3", persisted.totalVolumeM3);
            
            miningTelemetryAggregator_->restoreSession(persisted);
            
            // Update status with restored data
            const auto now = std::chrono::system_clock::now();
            status_.telemetry.mining = miningTelemetryAggregator_->snapshot(now);
            
            if (status_.telemetry.mining.has_value())
            {
                spdlog::info("After restore: status_.telemetry.mining has {:.1f} m3", 
                    status_.telemetry.mining->totalVolumeM3);
            }
            else
            {
                spdlog::error("After restore: status_.telemetry.mining is EMPTY!");
            }
            
            // Don't publish here - publishCallback_ isn't set yet since start() hasn't been called
            // The caller (HelperRuntime) will call forcePublish() after start()
        }
        else
        {
            spdlog::error("Cannot restore mining session: miningTelemetryAggregator_ is null");
        }
    }

    void LogWatcher::forcePublish()
    {
        std::lock_guard<std::mutex> guard(mutex_);
        
        spdlog::info("LogWatcher::forcePublish() called");
        
        if (status_.telemetry.mining.has_value())
        {
            spdlog::info("Before publish: mining has {:.1f} m3", status_.telemetry.mining->totalVolumeM3);
        }
        else
        {
            spdlog::warn("Before publish: mining is EMPTY!");
        }
        
        // Don't regenerate snapshots - just publish what's already in status_
        // This preserves restored session data that was set by restoreMiningSession()
        
        // Force publish the current state
        publishStateIfNeeded(status_, true);
        
        spdlog::info("Forced state publish completed");
    }

    std::string LogWatcher::buildStatusNotes(const LogWatcherStatus& snapshot)
    {
        std::ostringstream oss;
        if (snapshot.location.has_value())
        {
            oss << "Location: " << snapshot.location->systemName;
            if (!snapshot.chatFile.empty())
            {
                oss << " (" << snapshot.chatFile.filename().string() << ")";
            }
            if (!snapshot.location->systemId.empty() && snapshot.location->systemId != snapshot.location->systemName)
            {
                oss << " [" << snapshot.location->systemId << "]";
            }
            if (snapshot.location->observedAt.time_since_epoch().count() != 0)
            {
                oss << " @ " << format_time_utc(snapshot.location->observedAt);
            }
        }
        else
        {
            oss << "Location pending";
        }

        if (snapshot.combat.has_value())
        {
            oss << "; Combat events: " << snapshot.combat->combatEventCount;
            if (!snapshot.combat->characterId.empty())
            {
                oss << " (" << snapshot.combat->characterId << ")";
            }
            if (!snapshot.combat->lastCombatLine.empty())
            {
                oss << " last=" << snapshot.combat->lastCombatLine.substr(0, 80);
            }
        }
        else if (!snapshot.combatFile.empty())
        {
            oss << "; Combat log armed";
        }

        if (snapshot.telemetry.combat.has_value() && snapshot.telemetry.combat->hasData())
        {
            const auto flags = oss.flags();
            const auto precision = oss.precision();
            oss << "; Damage dealt " << std::fixed << std::setprecision(1) << snapshot.telemetry.combat->totalDamageDealt
                << " / taken " << snapshot.telemetry.combat->totalDamageTaken;
            oss.flags(flags);
            oss.precision(precision);
        }

        if (snapshot.telemetry.mining.has_value() && snapshot.telemetry.mining->hasData())
        {
            const auto flags = oss.flags();
            const auto precision = oss.precision();
            oss << "; Mined " << std::fixed << std::setprecision(1) << snapshot.telemetry.mining->totalVolumeM3 << " m3";
            oss.flags(flags);
            oss.precision(precision);
        }

        return oss.str();
    }

    std::uint64_t LogWatcher::now_ms()
    {
        const auto now = std::chrono::system_clock::now();
        return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count());
    }
}
