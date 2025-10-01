#pragma once

#include <atomic>
#include <chrono>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

#include <httplib.h>
#include <nlohmann/json.hpp>

#include "overlay_schema.hpp"
#include "shared_memory_channel.hpp"
#include "event_channel.hpp"
#include "helper_websocket.hpp"

class HelperServer {
public:
    HelperServer(std::string host, int port, std::string authToken);
    ~HelperServer();

    HelperServer(const HelperServer&) = delete;
    HelperServer& operator=(const HelperServer&) = delete;

    bool start();
    void stop();

    bool isRunning() const noexcept;
    bool hasOverlayState() const noexcept { return hasOverlayState_.load(); }
    int port() const noexcept { return port_; }
    const std::string& host() const noexcept { return host_; }
    bool requiresAuth() const noexcept { return requireAuth_; }
    const std::string& authToken() const noexcept { return authToken_; }

    bool ingestOverlayState(const overlay::OverlayState& state, std::size_t requestBytes, std::string source);
    void recordOverlayEvents(std::vector<overlay::OverlayEvent> events, std::uint32_t dropped);

    struct OverlayEventStats
    {
        std::uint64_t recorded{0};
        std::uint32_t dropped{0};
        std::size_t buffered{0};
    };

    OverlayEventStats getOverlayEventStats() const;

    struct OverlayStateStats
    {
        bool hasState{false};
        std::uint64_t generatedAtMs{0};
        std::optional<std::chrono::system_clock::time_point> acceptedAt;
    };

    OverlayStateStats getOverlayStateStats() const;

private:
    void configureRoutes();
    bool authorize(const httplib::Request& req, httplib::Response& res) const;
    long long uptimeMilliseconds() const;
    std::optional<nlohmann::json> latestOverlayStateJson() const;

    std::string host_;
    int port_;
    httplib::Server server_;
    std::thread serverThread_;
    std::atomic_bool running_{false};
    std::atomic_bool hasOverlayState_{false};
    std::chrono::steady_clock::time_point startedAt_{};
    std::chrono::steady_clock::time_point stoppedAt_{};

    mutable std::mutex overlayStateMutex_;
    std::string latestOverlayState_;
    nlohmann::json latestOverlayStateJson_;
    std::uint64_t lastOverlayGeneratedAtMs_{0};
    std::chrono::system_clock::time_point lastOverlayAcceptedAt_{};
    std::string authToken_;
    bool requireAuth_{false};
    overlay::SharedMemoryWriter sharedMemoryWriter_;

    struct EventRecord
    {
        std::uint64_t id;
        overlay::OverlayEvent event;
    };

    mutable std::mutex eventsMutex_;
    std::deque<EventRecord> recentEvents_;
    std::uint64_t nextEventId_{1};
    std::uint32_t droppedEvents_{0};
    static constexpr std::size_t maxEventBuffer_ = 128;

    std::unique_ptr<helper::ws::HelperWebSocketHub> websocketHub_;
    int websocketPort_{0};
};
