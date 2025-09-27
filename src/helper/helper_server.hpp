#pragma once

#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <thread>

#include <httplib.h>
#include <nlohmann/json.hpp>

#include "overlay_schema.hpp"
#include "shared_memory_channel.hpp"

class HelperServer {
public:
    HelperServer(std::string host, int port, std::string authToken);
    ~HelperServer();

    HelperServer(const HelperServer&) = delete;
    HelperServer& operator=(const HelperServer&) = delete;

    bool start();
    void stop();

    bool isRunning() const noexcept;
    int port() const noexcept { return port_; }
    const std::string& host() const noexcept { return host_; }
    bool requiresAuth() const noexcept { return requireAuth_; }
    const std::string& authToken() const noexcept { return authToken_; }

    bool ingestOverlayState(const overlay::OverlayState& state, std::size_t requestBytes, std::string source);

private:
    void configureRoutes();
    bool authorize(const httplib::Request& req, httplib::Response& res) const;
    long long uptimeMilliseconds() const;

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
    std::string authToken_;
    bool requireAuth_{false};
    overlay::SharedMemoryWriter sharedMemoryWriter_;
};
