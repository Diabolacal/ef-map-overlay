#include "session_tracker.hpp"

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <chrono>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace helper
{
    void AllTimeVisitedSystems::recordVisit(const std::string& system_id, const std::string& system_name)
    {
        auto it = systems.find(system_id);
        if (it != systems.end())
        {
            it->second.visits++;
        }
        else
        {
            systems[system_id] = SystemVisitData{system_name, 1};
        }
    }

    void SessionVisitedSystems::recordVisit(const std::string& system_id, const std::string& system_name)
    {
        auto it = systems.find(system_id);
        if (it != systems.end())
        {
            it->second.visits++;
        }
        else
        {
            systems[system_id] = SystemVisitData{system_name, 1};
        }
    }

    SessionTracker::SessionTracker(std::filesystem::path data_directory)
        : dataDirectory_(std::move(data_directory))
        , allTimeFilePath_(dataDirectory_ / "visited_systems.json")
    {
        // Ensure data directory exists
        if (!std::filesystem::exists(dataDirectory_))
        {
            std::error_code ec;
            std::filesystem::create_directories(dataDirectory_, ec);
            if (ec)
            {
                spdlog::error("Failed to create session tracker data directory: {}", ec.message());
            }
        }

        // Load existing all-time data
        loadAllTime();
    }

    void SessionTracker::setAllTimeTrackingEnabled(bool enabled)
    {
        {
            std::lock_guard<std::mutex> lock(allTimeMutex_);
            allTimeData_.tracking_enabled = enabled;
            allTimeData_.last_updated_ms = nowMs();
        }
        saveAllTime();
    }

    bool SessionTracker::isAllTimeTrackingEnabled() const
    {
        std::lock_guard<std::mutex> lock(allTimeMutex_);
        return allTimeData_.tracking_enabled;
    }

    void SessionTracker::recordSystemVisitAllTime(const std::string& system_id, const std::string& system_name)
    {
        {
            std::lock_guard<std::mutex> lock(allTimeMutex_);
            if (!allTimeData_.tracking_enabled)
            {
                return;
            }
            allTimeData_.recordVisit(system_id, system_name);
            allTimeData_.last_updated_ms = nowMs();
        }
        saveAllTime();
    }

    void SessionTracker::resetAllTimeTracking()
    {
        {
            std::lock_guard<std::mutex> lock(allTimeMutex_);
            allTimeData_.systems.clear();
            allTimeData_.last_updated_ms = nowMs();
        }
        saveAllTime();
    }

    AllTimeVisitedSystems SessionTracker::getAllTimeData() const
    {
        std::lock_guard<std::mutex> lock(allTimeMutex_);
        return allTimeData_;
    }

    std::string SessionTracker::startSession()
    {
        std::lock_guard<std::recursive_mutex> lock(sessionMutex_);
        
        // Stop any existing active session first
        if (activeSession_.has_value())
        {
            activeSession_->active = false;
            activeSession_->end_time_ms = nowMs();
            
            // Save the finished session
            const std::string finished_id = activeSession_->session_id;
            const auto session_path = getSessionFilePath(finished_id);
            
            try
            {
                nlohmann::json json;
                json["version"] = activeSession_->version;
                json["session_id"] = activeSession_->session_id;
                json["start_time_ms"] = activeSession_->start_time_ms;
                json["end_time_ms"] = activeSession_->end_time_ms;
                json["active"] = false;
                
                nlohmann::json systems_obj = nlohmann::json::object();
                for (const auto& [system_id, data] : activeSession_->systems)
                {
                    systems_obj[system_id] = {
                        {"name", data.name},
                        {"visits", data.visits}
                    };
                }
                json["systems"] = systems_obj;
                
                std::ofstream file(session_path);
                if (file.is_open())
                {
                    file << json.dump(2);
                    spdlog::info("Saved finished session: {}", finished_id);
                }
            }
            catch (const std::exception& ex)
            {
                spdlog::error("Failed to save finished session {}: {}", finished_id, ex.what());
            }
        }

        // Create new session
        SessionVisitedSystems new_session;
        new_session.session_id = generateSessionId();
        new_session.start_time_ms = nowMs();
        new_session.active = true;
        activeSession_ = new_session;
        
        spdlog::info("Started new session: {}", new_session.session_id);
        saveActiveSession();
        
        return new_session.session_id;
    }

    void SessionTracker::stopSession()
    {
        std::lock_guard<std::recursive_mutex> lock(sessionMutex_);
        
        if (!activeSession_.has_value())
        {
            spdlog::warn("No active session to stop");
            return;
        }

        activeSession_->active = false;
        activeSession_->end_time_ms = nowMs();
        
        const std::string session_id = activeSession_->session_id;
        spdlog::info("Stopped session: {}", session_id);
        
        // Save final state
        const auto session_path = getSessionFilePath(session_id);
        try
        {
            nlohmann::json json;
            json["version"] = activeSession_->version;
            json["session_id"] = activeSession_->session_id;
            json["start_time_ms"] = activeSession_->start_time_ms;
            json["end_time_ms"] = activeSession_->end_time_ms;
            json["active"] = false;
            
            nlohmann::json systems_obj = nlohmann::json::object();
            for (const auto& [system_id, data] : activeSession_->systems)
            {
                systems_obj[system_id] = {
                    {"name", data.name},
                    {"visits", data.visits}
                };
            }
            json["systems"] = systems_obj;
            
            std::ofstream file(session_path);
            if (file)
            {
                file << json.dump(2);
                spdlog::info("Session saved to: {}", session_path.string());
            }
            else
            {
                spdlog::error("Failed to save session to: {}", session_path.string());
            }
        }
        catch (const std::exception& e)
        {
            spdlog::error("Error saving session: {}", e.what());
        }
        
        activeSession_.reset();
    }

    void SessionTracker::resetActiveSession()
    {
        std::lock_guard<std::recursive_mutex> lock(sessionMutex_);
        
        if (!activeSession_.has_value())
        {
            spdlog::info("No active session to reset");
            return;
        }

        // Clear all systems but keep the session active
        activeSession_->systems.clear();
        spdlog::info("Reset active session: {} (cleared all systems)", activeSession_->session_id);
        
        // Save the cleared state
        saveActiveSession();
    }

    bool SessionTracker::hasActiveSession() const
    {
        std::lock_guard<std::recursive_mutex> lock(sessionMutex_);
        return activeSession_.has_value() && activeSession_->active;
    }

    std::optional<std::string> SessionTracker::getActiveSessionId() const
    {
        std::lock_guard<std::recursive_mutex> lock(sessionMutex_);
        if (activeSession_.has_value() && activeSession_->active)
        {
            return activeSession_->session_id;
        }
        return std::nullopt;
    }

    void SessionTracker::recordSystemVisitSession(const std::string& system_id, const std::string& system_name)
    {
        {
            std::lock_guard<std::recursive_mutex> lock(sessionMutex_);
            if (!activeSession_.has_value() || !activeSession_->active)
            {
                return;
            }
            activeSession_->recordVisit(system_id, system_name);
        }
        saveActiveSession();
    }

    std::optional<SessionVisitedSystems> SessionTracker::getSessionData(const std::string& session_id) const
    {
        const auto session_path = getSessionFilePath(session_id);
        
        if (!std::filesystem::exists(session_path))
        {
            return std::nullopt;
        }

        try
        {
            std::ifstream file(session_path);
            if (!file.is_open())
            {
                return std::nullopt;
            }

            nlohmann::json json;
            file >> json;

            SessionVisitedSystems session;
            session.version = json.value("version", 1);
            session.session_id = json.value("session_id", "");
            session.start_time_ms = json.value("start_time_ms", 0ULL);
            session.end_time_ms = json.value("end_time_ms", 0ULL);
            session.active = json.value("active", false);

            if (json.contains("systems") && json["systems"].is_object())
            {
                for (auto& [system_id, data] : json["systems"].items())
                {
                    SystemVisitData visit;
                    visit.name = data.value("name", "");
                    visit.visits = data.value("visits", 0ULL);
                    session.systems[system_id] = visit;
                }
            }

            return session;
        }
        catch (const std::exception& ex)
        {
            spdlog::error("Failed to load session {}: {}", session_id, ex.what());
            return std::nullopt;
        }
    }

    std::optional<SessionVisitedSystems> SessionTracker::getActiveSessionData() const
    {
        std::lock_guard<std::recursive_mutex> lock(sessionMutex_);
        return activeSession_;
    }

    bool SessionTracker::saveAllTime()
    {
        std::lock_guard<std::mutex> lock(allTimeMutex_);
        
        try
        {
            nlohmann::json json;
            json["version"] = allTimeData_.version;
            json["tracking_enabled"] = allTimeData_.tracking_enabled;
            json["last_updated_ms"] = allTimeData_.last_updated_ms;
            
            nlohmann::json systems_obj = nlohmann::json::object();
            for (const auto& [system_id, data] : allTimeData_.systems)
            {
                systems_obj[system_id] = {
                    {"name", data.name},
                    {"visits", data.visits}
                };
            }
            json["systems"] = systems_obj;

            std::ofstream file(allTimeFilePath_);
            if (!file.is_open())
            {
                spdlog::error("Failed to open all-time tracking file for writing: {}", allTimeFilePath_.string());
                return false;
            }

            file << json.dump(2);
            return true;
        }
        catch (const std::exception& ex)
        {
            spdlog::error("Failed to save all-time tracking data: {}", ex.what());
            return false;
        }
    }

    bool SessionTracker::saveActiveSession()
    {
        std::lock_guard<std::recursive_mutex> lock(sessionMutex_);
        
        if (!activeSession_.has_value())
        {
            return true;
        }

        const auto session_path = getSessionFilePath(activeSession_->session_id);
        
        try
        {
            nlohmann::json json;
            json["version"] = activeSession_->version;
            json["session_id"] = activeSession_->session_id;
            json["start_time_ms"] = activeSession_->start_time_ms;
            json["end_time_ms"] = activeSession_->end_time_ms;
            json["active"] = activeSession_->active;
            
            nlohmann::json systems_obj = nlohmann::json::object();
            for (const auto& [system_id, data] : activeSession_->systems)
            {
                systems_obj[system_id] = {
                    {"name", data.name},
                    {"visits", data.visits}
                };
            }
            json["systems"] = systems_obj;

            std::ofstream file(session_path);
            if (!file.is_open())
            {
                spdlog::error("Failed to open session file for writing: {}", session_path.string());
                return false;
            }

            file << json.dump(2);
            return true;
        }
        catch (const std::exception& ex)
        {
            spdlog::error("Failed to save active session: {}", ex.what());
            return false;
        }
    }

    bool SessionTracker::loadAllTime()
    {
        std::lock_guard<std::mutex> lock(allTimeMutex_);
        
        if (!std::filesystem::exists(allTimeFilePath_))
        {
            spdlog::info("No existing all-time tracking data found, starting fresh");
            return true;
        }

        try
        {
            std::ifstream file(allTimeFilePath_);
            if (!file.is_open())
            {
                spdlog::warn("Failed to open all-time tracking file: {}", allTimeFilePath_.string());
                return false;
            }

            nlohmann::json json;
            file >> json;

            allTimeData_.version = json.value("version", 1);
            allTimeData_.tracking_enabled = json.value("tracking_enabled", false);
            allTimeData_.last_updated_ms = json.value("last_updated_ms", 0ULL);

            if (json.contains("systems") && json["systems"].is_object())
            {
                for (auto& [system_id, data] : json["systems"].items())
                {
                    SystemVisitData visit;
                    visit.name = data.value("name", "");
                    visit.visits = data.value("visits", 0ULL);
                    allTimeData_.systems[system_id] = visit;
                }
            }

            spdlog::info("Loaded all-time tracking data: {} systems tracked", allTimeData_.systems.size());
            return true;
        }
        catch (const std::exception& ex)
        {
            spdlog::error("Failed to load all-time tracking data: {}", ex.what());
            return false;
        }
    }

    std::filesystem::path SessionTracker::getSessionFilePath(const std::string& session_id) const
    {
        return dataDirectory_ / (session_id + ".json");
    }

    std::vector<SessionVisitedSystems> SessionTracker::listStoppedSessions() const
    {
        std::vector<SessionVisitedSystems> sessions;
        
        try
        {
            if (!std::filesystem::exists(dataDirectory_))
            {
                return sessions;
            }

            for (const auto& entry : std::filesystem::directory_iterator(dataDirectory_))
            {
                if (!entry.is_regular_file())
                {
                    continue;
                }

                const auto& path = entry.path();
                const auto filename = path.filename().string();
                
                // Skip non-session files
                if (!filename.starts_with("session_") || !filename.ends_with(".json"))
                {
                    continue;
                }

                // Skip active session file
                if (filename == "active_session.json")
                {
                    continue;
                }

                // Try to load and parse session file
                try
                {
                    std::ifstream file(path);
                    if (!file)
                    {
                        continue;
                    }

                    nlohmann::json json;
                    file >> json;

                    SessionVisitedSystems session;
                    session.version = json.value("version", 1);
                    session.session_id = json.value("session_id", "");
                    session.start_time_ms = json.value("start_time_ms", 0ULL);
                    session.end_time_ms = json.value("end_time_ms", 0ULL);
                    session.active = json.value("active", false);

                    // Only include stopped sessions (not active)
                    if (session.active)
                    {
                        continue;
                    }

                    // Load systems count but not full data (for performance)
                    if (json.contains("systems") && json["systems"].is_object())
                    {
                        for (const auto& [system_id, data] : json["systems"].items())
                        {
                            SystemVisitData visit;
                            visit.name = data.value("name", "");
                            visit.visits = data.value("visits", 0ULL);
                            session.systems[system_id] = visit;
                        }
                    }

                    sessions.push_back(std::move(session));
                }
                catch (const std::exception& ex)
                {
                    spdlog::warn("Failed to parse session file {}: {}", filename, ex.what());
                }
            }

            // Sort by start time (newest first)
            std::sort(sessions.begin(), sessions.end(), 
                [](const SessionVisitedSystems& a, const SessionVisitedSystems& b) {
                    return a.start_time_ms > b.start_time_ms;
                });
        }
        catch (const std::exception& ex)
        {
            spdlog::error("Error listing sessions: {}", ex.what());
        }

        return sessions;
    }

    std::string SessionTracker::generateSessionId() const
    {
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        std::tm tm;
        
#ifdef _WIN32
        localtime_s(&tm, &time_t);
#else
        localtime_r(&time_t, &tm);
#endif

        std::ostringstream oss;
        oss << "session_"
            << std::put_time(&tm, "%Y%m%d_%H%M%S");
        
        return oss.str();
    }

    std::uint64_t SessionTracker::nowMs() const
    {
        return static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch())
                .count());
    }
}
