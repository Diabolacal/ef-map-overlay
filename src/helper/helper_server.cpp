#include "helper_server.hpp"

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <cstdlib>
#include <sstream>
#include <utility>
#include <utility>

namespace
{
    constexpr const char* application_json = "application/json";
    constexpr const char* text_plain = "text/plain";

    nlohmann::json make_error(std::string_view message)
    {
        return nlohmann::json{{"status", "error"}, {"message", message}};
    }

    std::uint64_t now_ms()
    {
        const auto now = std::chrono::system_clock::now();
        return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count());
    }
}

HelperServer::HelperServer(std::string host, int port, std::string authToken)
    : host_(std::move(host))
    , port_(port)
    , authToken_(std::move(authToken))
    , requireAuth_(!authToken_.empty())
{
    websocketPort_ = port_ > 0 ? port_ + 1 : 0;

    helper::ws::HelperWebSocketHub::Config wsConfig;
    wsConfig.host = host_;
    wsConfig.port = websocketPort_;
    wsConfig.httpPort = port_;
    wsConfig.token = authToken_;
    wsConfig.getLatestOverlayState = [this]() { return latestOverlayStateJson(); };
    websocketHub_ = std::make_unique<helper::ws::HelperWebSocketHub>(std::move(wsConfig));

    configureRoutes();
}

void HelperServer::setTelemetrySummaryHandler(TelemetrySummaryHandler handler)
{
    telemetrySummaryHandler_ = std::move(handler);
}

void HelperServer::setTelemetryResetHandler(TelemetryResetHandler handler)
{
    telemetryResetHandler_ = std::move(handler);
}

void HelperServer::setFollowModeProvider(FollowModeProvider provider)
{
    followModeProvider_ = std::move(provider);
}

void HelperServer::setFollowModeUpdateHandler(FollowModeUpdateHandler handler)
{
    followModeUpdateHandler_ = std::move(handler);
}

bool HelperServer::updateFollowModeFlag(bool enabled)
{
    std::string serialized;
    nlohmann::json json;
    std::uint32_t version = overlay::schema_version;
    std::uint64_t generatedAt = 0;

    {
        std::lock_guard<std::mutex> guard(overlayStateMutex_);
        if (!hasOverlayState_.load() || latestOverlayState_.empty())
        {
            return false;
        }

        latestOverlayStateJson_["follow_mode_enabled"] = enabled;
        latestOverlayStateJson_["heartbeat_ms"] = now_ms();
        json = latestOverlayStateJson_;
        serialized = json.dump();
        latestOverlayState_ = serialized;
        if (json.contains("version"))
        {
            version = json.at("version").get<std::uint32_t>();
        }
        if (json.contains("generated_at_ms"))
        {
            generatedAt = json.at("generated_at_ms").get<std::uint64_t>();
        }
        lastOverlayAcceptedAt_ = std::chrono::system_clock::now();
        lastOverlayGeneratedAtMs_ = generatedAt;
    }

    const bool sharedOk = sharedMemoryWriter_.write(serialized, version, generatedAt);
    if (!sharedOk)
    {
        spdlog::warn("Failed to publish follow mode update to shared memory");
    }

    if (websocketHub_)
    {
        nlohmann::json envelope{{"type", "overlay_state"}, {"state", json}};
        websocketHub_->broadcastOverlayState(std::move(envelope));
    }

    return true;
}

HelperServer::~HelperServer()
{
    stop();
}

bool HelperServer::start()
{
    if (running_.load())
    {
        spdlog::warn("HelperServer already running on {}:{}", host_, port_);
        return true;
    }

    if (!server_.bind_to_port(host_.c_str(), port_))
    {
        spdlog::error("Failed to bind helper server to {}:{}", host_, port_);
        return false;
    }

    if (websocketHub_)
    {
        if (!websocketHub_->start())
        {
            spdlog::error("Failed to start helper WebSocket hub on {}:{}", host_, websocketPort_);
        }
        else
        {
            websocketPort_ = websocketHub_->port();
            spdlog::info("Helper WebSocket hub ready on {}:{}", host_, websocketPort_);
        }
    }

    running_.store(true);
    startedAt_ = std::chrono::steady_clock::now();
    stoppedAt_ = startedAt_;

    startHeartbeat();

    serverThread_ = std::thread([this]() {
    spdlog::info("Helper server listening on {}:{} (auth: {})", host_, port_, requireAuth_ ? "required" : "disabled");
        server_.listen_after_bind();
        spdlog::info("Helper server shutdown complete.");
        stoppedAt_ = std::chrono::steady_clock::now();
        running_.store(false);
    });

    return true;
}

void HelperServer::stop()
{
    if (!running_.load())
    {
        return;
    }

    stopHeartbeat();

    if (websocketHub_)
    {
        websocketHub_->stop();
    }

    server_.stop();

    if (serverThread_.joinable())
    {
        serverThread_.join();
    }

    stoppedAt_ = std::chrono::steady_clock::now();
}

bool HelperServer::isRunning() const noexcept
{
    return running_.load();
}

long long HelperServer::uptimeMilliseconds() const
{
    if (startedAt_ == std::chrono::steady_clock::time_point{})
    {
        return 0;
    }

    const auto end = running_.load() ? std::chrono::steady_clock::now() : stoppedAt_;
    return std::chrono::duration_cast<std::chrono::milliseconds>(end - startedAt_).count();
}

std::optional<nlohmann::json> HelperServer::latestOverlayStateJson() const
{
    std::lock_guard<std::mutex> guard(overlayStateMutex_);
    if (!hasOverlayState_.load() || latestOverlayStateJson_.is_null())
    {
        return std::nullopt;
    }
    return latestOverlayStateJson_;
}

bool HelperServer::authorize(const httplib::Request& req, httplib::Response& res) const
{
    if (!requireAuth_)
    {
        return true;
    }

    const auto header = req.get_header_value("x-ef-overlay-token");
    if (!header.empty() && header == authToken_)
    {
        return true;
    }

    if (req.has_param("token"))
    {
        const auto queryToken = req.get_param_value("token");
        if (queryToken == authToken_)
        {
            return true;
        }
    }

    res.set_content(make_error("Unauthorized").dump(), application_json);
    res.status = 401;
    return false;
}

bool HelperServer::ingestOverlayState(const overlay::OverlayState& state, std::size_t requestBytes, std::string source)
{
    auto enriched = state;
    const auto heartbeat = now_ms();
    if (enriched.generated_at_ms == 0)
    {
        enriched.generated_at_ms = heartbeat;
    }
    enriched.heartbeat_ms = heartbeat;
    enriched.source_online = true;

    const auto stateJson = overlay::serialize_overlay_state(enriched);
    const auto serialized = stateJson.dump();

    {
        std::lock_guard<std::mutex> guard(overlayStateMutex_);
        latestOverlayState_ = serialized;
        latestOverlayStateJson_ = stateJson;
        lastOverlayGeneratedAtMs_ = enriched.generated_at_ms;
        lastOverlayAcceptedAt_ = std::chrono::system_clock::now();
    }

    hasOverlayState_.store(true);

    const bool sharedOk = sharedMemoryWriter_.write(serialized, static_cast<std::uint32_t>(enriched.version), enriched.generated_at_ms);
    if (!sharedOk)
    {
        spdlog::warn("Overlay state accepted via {} but failed to publish to shared memory", source);
    }

    if (websocketHub_)
    {
        nlohmann::json envelope{{"type", "overlay_state"}, {"state", stateJson}};
        websocketHub_->broadcastOverlayState(envelope);
    }

    spdlog::info("Overlay state accepted via {} ({} bytes)", std::move(source), static_cast<unsigned long long>(requestBytes));
    return true;
}

void HelperServer::publishOfflineState()
{
    if (!hasOverlayState_.load())
    {
        return;
    }

    std::string serialized;
    nlohmann::json json;
    std::uint32_t version = overlay::schema_version;
    std::uint64_t generatedAt = 0;

    {
        std::lock_guard<std::mutex> guard(overlayStateMutex_);
        if (latestOverlayState_.empty())
        {
            return;
        }

        json = latestOverlayStateJson_;
        json["source_online"] = false;
        json["heartbeat_ms"] = now_ms();
        serialized = json.dump();
        latestOverlayState_ = serialized;
        latestOverlayStateJson_ = json;
        if (json.contains("version"))
        {
            version = json.at("version").get<std::uint32_t>();
        }
        if (json.contains("generated_at_ms"))
        {
            generatedAt = json.at("generated_at_ms").get<std::uint64_t>();
        }
        lastOverlayGeneratedAtMs_ = generatedAt;
        lastOverlayAcceptedAt_ = std::chrono::system_clock::now();
    }

    const bool sharedOk = sharedMemoryWriter_.write(serialized, version, generatedAt);
    if (!sharedOk)
    {
        spdlog::warn("Failed to publish offline overlay state to shared memory");
    }

    if (websocketHub_)
    {
        nlohmann::json envelope{{"type", "overlay_state"}, {"state", json}};
        websocketHub_->broadcastOverlayState(envelope);
    }

    spdlog::info("Overlay source marked offline");
}

void HelperServer::startHeartbeat()
{
    if (heartbeatRunning_.load())
    {
        return;
    }

    heartbeatRunning_.store(true);
    heartbeatThread_ = std::thread([this]() {
        while (heartbeatRunning_.load())
        {
            std::this_thread::sleep_for(heartbeatInterval_);
            if (!hasOverlayState_.load())
            {
                continue;
            }

            std::string serialized;
            std::uint32_t version = overlay::schema_version;
            std::uint64_t generatedAt = 0;

            {
                std::lock_guard<std::mutex> guard(overlayStateMutex_);
                if (latestOverlayState_.empty())
                {
                    continue;
                }

                latestOverlayStateJson_["heartbeat_ms"] = now_ms();
                latestOverlayStateJson_["source_online"] = true;
                serialized = latestOverlayStateJson_.dump();
                latestOverlayState_ = serialized;
                if (latestOverlayStateJson_.contains("version"))
                {
                    version = latestOverlayStateJson_.at("version").get<std::uint32_t>();
                }
                if (latestOverlayStateJson_.contains("generated_at_ms"))
                {
                    generatedAt = latestOverlayStateJson_.at("generated_at_ms").get<std::uint64_t>();
                }
                lastOverlayGeneratedAtMs_ = generatedAt;
            }

            const bool sharedOk = sharedMemoryWriter_.write(serialized, version, generatedAt);
            if (!sharedOk)
            {
                spdlog::warn("Heartbeat publication failed to update shared memory");
            }
        }
    });
}

void HelperServer::stopHeartbeat()
{
    heartbeatRunning_.store(false);
    if (heartbeatThread_.joinable())
    {
        heartbeatThread_.join();
    }
}

void HelperServer::configureRoutes()
{
    using namespace std::chrono;

    server_.set_default_headers({
        {"Access-Control-Allow-Origin", "*"},
        {"Access-Control-Allow-Headers", "Content-Type, x-ef-overlay-token"},
        {"Access-Control-Allow-Methods", "GET, POST, OPTIONS"}
    });

    server_.Options(".*", [](const httplib::Request&, httplib::Response& res) {
        res.status = 204;
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_header("Access-Control-Allow-Headers", "Content-Type, x-ef-overlay-token");
        res.set_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    });

    server_.set_error_handler([](const httplib::Request&, httplib::Response& res) {
        res.set_content("Resource not found", text_plain);
        res.status = 404;
    });

    server_.set_exception_handler([](const httplib::Request&, httplib::Response& res, std::exception_ptr ep) {
        nlohmann::json payload = make_error("Unhandled exception");
        try
        {
            if (ep)
            {
                std::rethrow_exception(ep);
            }
        }
        catch (const std::exception& ex)
        {
            payload["message"] = ex.what();
        }
        res.set_content(payload.dump(), application_json);
        res.status = 500;
    });

    server_.Get("/health", [this](const httplib::Request&, httplib::Response& res) {
        const nlohmann::json payload{
            {"status", "ok"},
            {"uptime_ms", uptimeMilliseconds()},
            {"port", port_},
            {"ws_port", websocketPort_},
            {"has_overlay_state", hasOverlayState_.load()}
        };

        res.set_content(payload.dump(), application_json);
        res.status = 200;
    });

    server_.Get("/overlay/state", [this](const httplib::Request& req, httplib::Response& res) {
        if (!authorize(req, res))
        {
            return;
        }

        if (!hasOverlayState_.load())
        {
            res.set_content(make_error("No overlay state recorded").dump(), application_json);
            res.status = 404;
            return;
        }

        std::string snapshot;
        {
            std::lock_guard<std::mutex> guard(overlayStateMutex_);
            snapshot = latestOverlayState_;
        }

        res.set_content(snapshot, application_json);
        res.status = 200;
    });

    server_.Get("/overlay/events", [this](const httplib::Request& req, httplib::Response& res) {
        if (!authorize(req, res))
        {
            return;
        }

        std::uint64_t sinceId = 0;
        if (req.has_param("since"))
        {
            const auto& value = req.get_param_value("since");
            sinceId = static_cast<std::uint64_t>(::_strtoui64(value.c_str(), nullptr, 10));
        }

        nlohmann::json eventsJson = nlohmann::json::array();
        std::uint64_t latestId = sinceId;
        std::uint32_t dropped = 0;

        {
            std::lock_guard<std::mutex> guard(eventsMutex_);
            for (const auto& record : recentEvents_)
            {
                if (record.id <= sinceId)
                {
                    continue;
                }

                eventsJson.push_back({
                    {"id", record.id},
                    {"type", static_cast<std::uint32_t>(record.event.type)},
                    {"timestamp_ms", record.event.timestamp_ms},
                    {"payload", record.event.payload}
                });
                latestId = record.id;
            }
            dropped = droppedEvents_;
        }

        nlohmann::json payload{
            {"events", std::move(eventsJson)},
            {"next_since", latestId},
            {"dropped", dropped}
        };

        res.set_content(payload.dump(), application_json);
        res.status = 200;
    });

    server_.Get("/telemetry/current", [this](const httplib::Request& req, httplib::Response& res) {
        if (!authorize(req, res))
        {
            return;
        }

        if (!telemetrySummaryHandler_)
        {
            res.set_content(make_error("Telemetry summary unavailable").dump(), application_json);
            res.status = 503;
            return;
        }

        auto payload = telemetrySummaryHandler_();
        if (!payload.has_value())
        {
            res.set_content(make_error("Telemetry summary unavailable").dump(), application_json);
            res.status = 503;
            return;
        }

        res.set_content(payload->dump(), application_json);
        res.status = 200;
    });

    server_.Get("/telemetry/history", [this](const httplib::Request& req, httplib::Response& res) {
        if (!authorize(req, res))
        {
            return;
        }

        if (!telemetrySummaryHandler_)
        {
            res.set_content(make_error("Telemetry summary unavailable").dump(), application_json);
            res.status = 503;
            return;
        }

        auto payload = telemetrySummaryHandler_();
        if (!payload.has_value())
        {
            res.set_content(make_error("Telemetry summary unavailable").dump(), application_json);
            res.status = 503;
            return;
        }

        if (!payload->contains("history"))
        {
            res.set_content(make_error("Telemetry history unavailable").dump(), application_json);
            res.status = 404;
            return;
        }

        nlohmann::json response{
            {"status", "ok"},
            {"history", payload->at("history")}
        };

        res.set_content(response.dump(), application_json);
        res.status = 200;
    });

    server_.Get("/settings/follow", [this](const httplib::Request& req, httplib::Response& res) {
        if (!authorize(req, res))
        {
            return;
        }

        if (!followModeProvider_)
        {
            res.set_content(make_error("Follow mode provider unavailable").dump(), application_json);
            res.status = 503;
            return;
        }

        const bool enabled = followModeProvider_();
        nlohmann::json payload{{"status", "ok"}, {"enabled", enabled}};
        res.set_content(payload.dump(), application_json);
        res.status = 200;
    });

    server_.Post("/settings/follow", [this](const httplib::Request& req, httplib::Response& res) {
        if (!authorize(req, res))
        {
            return;
        }

        if (!followModeUpdateHandler_)
        {
            res.set_content(make_error("Follow mode update unavailable").dump(), application_json);
            res.status = 503;
            return;
        }

        auto json = nlohmann::json::parse(req.body, nullptr, false);
        if (json.is_discarded() || !json.contains("enabled"))
        {
            res.set_content(make_error("Request body must include 'enabled' boolean").dump(), application_json);
            res.status = 400;
            return;
        }

        bool enabled = false;
        try
        {
            enabled = json.at("enabled").get<bool>();
        }
        catch (const std::exception&)
        {
            res.set_content(make_error("'enabled' must be a boolean").dump(), application_json);
            res.status = 400;
            return;
        }

        const bool applied = followModeUpdateHandler_(enabled);
        nlohmann::json payload{{"status", applied ? "ok" : "accepted"}, {"enabled", enabled}};
        res.set_content(payload.dump(), application_json);
        res.status = applied ? 200 : 202;
    });

    server_.Post("/telemetry/reset", [this](const httplib::Request& req, httplib::Response& res) {
        if (!authorize(req, res))
        {
            return;
        }

        if (!telemetryResetHandler_)
        {
            res.set_content(make_error("Telemetry reset unavailable").dump(), application_json);
            res.status = 503;
            return;
        }

        auto payload = telemetryResetHandler_();
        if (!payload.has_value())
        {
            res.set_content(make_error("Telemetry reset failed").dump(), application_json);
            res.status = 500;
            return;
        }

        res.set_content(payload->dump(), application_json);
        res.status = 200;
    });

    server_.Get("/overlay/catalog", [this](const httplib::Request& req, httplib::Response& res) {
        if (!authorize(req, res))
        {
            return;
        }

        const auto summary = getStarCatalogSummary();

        nlohmann::json payload{
            {"loaded", summary.loaded},
            {"version", summary.version},
            {"record_count", summary.record_count}
        };

        if (!summary.path.empty())
        {
            payload["path"] = summary.path.string();
        }
        else
        {
            payload["path"] = nullptr;
        }

        if (summary.loaded)
        {
            payload["bbox"] = {
                {"min", {summary.bbox_min.x, summary.bbox_min.y, summary.bbox_min.z}},
                {"max", {summary.bbox_max.x, summary.bbox_max.y, summary.bbox_max.z}}
            };
        }

        if (!summary.error.empty())
        {
            payload["error"] = summary.error;
        }

        res.set_content(payload.dump(), application_json);
        res.status = summary.loaded ? 200 : 503;
    });

    server_.Post("/overlay/state", [this](const httplib::Request& req, httplib::Response& res) {
        if (!authorize(req, res))
        {
            return;
        }

        auto json = nlohmann::json::parse(req.body, nullptr, false);
        if (json.is_discarded())
        {
            res.set_content(make_error("Request body must be valid JSON").dump(), application_json);
            res.status = 400;
            return;
        }

        overlay::OverlayState state;
        try
        {
            state = overlay::parse_overlay_state(json);
        }
        catch (const std::exception& ex)
        {
            res.set_content(make_error(ex.what()).dump(), application_json);
            res.status = 400;
            return;
        }

        const auto bytes = req.body.size();

        ingestOverlayState(state, bytes, "http");

        const nlohmann::json payload{
            {"status", "accepted"},
            {"bytes", bytes}
        };

        res.set_content(payload.dump(), application_json);
        res.status = 202;
    });
}

void HelperServer::recordOverlayEvents(std::vector<overlay::OverlayEvent> events, std::uint32_t dropped)
{
    if (events.empty() && dropped == 0)
    {
        return;
    }

    nlohmann::json wsEvents = nlohmann::json::array();
    std::uint32_t droppedSnapshot = 0;
    std::uint64_t latestId = 0;

    {
        std::lock_guard<std::mutex> guard(eventsMutex_);
        if (dropped > droppedEvents_)
        {
            droppedEvents_ = dropped;
        }

        for (auto& event : events)
        {
            const auto assignedId = nextEventId_++;
            wsEvents.push_back({
                {"id", assignedId},
                {"type", static_cast<std::uint32_t>(event.type)},
                {"timestamp_ms", event.timestamp_ms},
                {"payload", event.payload}
            });

            recentEvents_.push_back(EventRecord{assignedId, std::move(event)});
            if (recentEvents_.size() > maxEventBuffer_)
            {
                recentEvents_.pop_front();
            }

            latestId = assignedId;
        }

        droppedSnapshot = droppedEvents_;
    }

    if (!events.empty())
    {
        spdlog::debug("Recorded {} overlay event(s); dropped={} total={}", events.size(), dropped, droppedSnapshot);
    }

    if (websocketHub_ && (!wsEvents.empty() || droppedSnapshot > 0))
    {
        nlohmann::json batch{{"events", wsEvents}, {"dropped", droppedSnapshot}};
        if (latestId != 0)
        {
            batch["next_since"] = latestId;
        }
        websocketHub_->broadcastEventBatch(std::move(batch));
    }
}

HelperServer::OverlayEventStats HelperServer::getOverlayEventStats() const
{
    std::lock_guard<std::mutex> guard(eventsMutex_);
    OverlayEventStats stats;
    stats.recorded = nextEventId_ > 0 ? nextEventId_ - 1 : 0;
    stats.dropped = droppedEvents_;
    stats.buffered = recentEvents_.size();
    return stats;
}

HelperServer::OverlayStateStats HelperServer::getOverlayStateStats() const
{
    std::lock_guard<std::mutex> guard(overlayStateMutex_);
    OverlayStateStats stats;
    stats.hasState = hasOverlayState_.load();
    if (stats.hasState)
    {
        stats.generatedAtMs = lastOverlayGeneratedAtMs_;
        if (lastOverlayAcceptedAt_ != std::chrono::system_clock::time_point{})
        {
            stats.acceptedAt = lastOverlayAcceptedAt_;
        }
    }
    return stats;
}

void HelperServer::updateStarCatalogSummary(StarCatalogSummary summary)
{
    std::lock_guard<std::mutex> guard(catalogMutex_);
    starCatalogSummary_ = std::move(summary);
}

HelperServer::StarCatalogSummary HelperServer::getStarCatalogSummary() const
{
    std::lock_guard<std::mutex> guard(catalogMutex_);
    return starCatalogSummary_;
}
