#pragma once

#include <chrono>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

namespace helper
{
    struct SystemVisitData
    {
        std::string name;
        std::uint64_t visits{0};
    };

    struct AllTimeVisitedSystems
    {
        int version{1};
        bool tracking_enabled{false};
        std::unordered_map<std::string, SystemVisitData> systems;
        std::uint64_t last_updated_ms{0};

        void recordVisit(const std::string& system_id, const std::string& system_name);
    };

    struct SessionVisitedSystems
    {
        int version{1};
        std::string session_id;
        std::uint64_t start_time_ms{0};
        std::uint64_t end_time_ms{0};
        bool active{false};
        std::unordered_map<std::string, SystemVisitData> systems;

        void recordVisit(const std::string& system_id, const std::string& system_name);
    };

    class SessionTracker
    {
    public:
        explicit SessionTracker(std::filesystem::path data_directory);

        // All-time tracking
        void setAllTimeTrackingEnabled(bool enabled);
        bool isAllTimeTrackingEnabled() const;
        void recordSystemVisitAllTime(const std::string& system_id, const std::string& system_name);
        void resetAllTimeTracking();
        AllTimeVisitedSystems getAllTimeData() const;

        // Session tracking
        std::string startSession();
        void stopSession();
        void resetActiveSession();
        bool hasActiveSession() const;
        std::optional<std::string> getActiveSessionId() const;
        void recordSystemVisitSession(const std::string& system_id, const std::string& system_name);
        std::optional<SessionVisitedSystems> getSessionData(const std::string& session_id) const;
        std::optional<SessionVisitedSystems> getActiveSessionData() const;
        std::vector<SessionVisitedSystems> listStoppedSessions() const;

        // Persistence
        bool saveAllTime();
        bool saveActiveSession();
        bool loadAllTime();

    private:
        std::filesystem::path dataDirectory_;
        std::filesystem::path allTimeFilePath_;
        
        mutable std::mutex allTimeMutex_;
        AllTimeVisitedSystems allTimeData_;

        mutable std::recursive_mutex sessionMutex_;
        std::optional<SessionVisitedSystems> activeSession_;

        std::filesystem::path getSessionFilePath(const std::string& session_id) const;
        std::string generateSessionId() const;
        std::uint64_t nowMs() const;
    };
}
