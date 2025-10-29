#include "helper_server.hpp"
#include "session_tracker.hpp"

#include <httplib.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <cstdlib>
#include <sstream>
#include <utility>
#include <utility>
#include <Windows.h>
#include <ShlObj.h>

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

    // Helper: Convert UTF-8 to wide string
    std::wstring utf8_to_wstring(const std::string& utf8)
    {
        if (utf8.empty()) return {};
        
        int wcharCount = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()), nullptr, 0);
        if (wcharCount <= 0) return {};
        
        std::wstring wide(wcharCount, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()), wide.data(), wcharCount);
        return wide;
    }

    // Helper: Convert wide string to UTF-8
    std::string wstring_to_utf8(const std::wstring& wide)
    {
        if (wide.empty()) return {};
        
        int utf8Count = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
        if (utf8Count <= 0) return {};
        
        std::string utf8(utf8Count, '\0');
        WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()), utf8.data(), utf8Count, nullptr, nullptr);
        return utf8;
    }

    // Load custom log base path from registry (empty string = use default)
    std::string loadCustomLogBasePath()
    {
        HKEY hKey;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\EF Map Overlay\\Settings", 0, KEY_READ, &hKey) != ERROR_SUCCESS)
        {
            return {};  // Key doesn't exist = no custom path
        }

        wchar_t buffer[MAX_PATH];
        DWORD bufferSize = sizeof(buffer);
        
        std::string result;
        if (RegQueryValueExW(hKey, L"LogBasePath", nullptr, nullptr, reinterpret_cast<LPBYTE>(buffer), &bufferSize) == ERROR_SUCCESS)
        {
            result = wstring_to_utf8(buffer);
        }
        
        RegCloseKey(hKey);
        return result;
    }

    // Save custom log base path to registry (empty string = delete/reset to default)
    bool saveCustomLogBasePath(const std::string& basePath)
    {
        HKEY hKey;
        DWORD disposition;
        if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\EF Map Overlay\\Settings", 0, nullptr, 0, KEY_WRITE, nullptr, &hKey, &disposition) != ERROR_SUCCESS)
        {
            return false;
        }

        bool success = true;
        if (basePath.empty())
        {
            // Delete the value to reset to default
            RegDeleteValueW(hKey, L"LogBasePath");
        }
        else
        {
            auto wide = utf8_to_wstring(basePath);
            success = (RegSetValueExW(hKey, L"LogBasePath", 0, REG_SZ, 
                reinterpret_cast<const BYTE*>(wide.c_str()), 
                static_cast<DWORD>((wide.size() + 1) * sizeof(wchar_t))) == ERROR_SUCCESS);
        }
        
        RegCloseKey(hKey);
        return success;
    }

    // Get the default Documents\Frontier\logs path
    std::string getDefaultLogBasePath()
    {
        PWSTR rawPath = nullptr;
        if (FAILED(SHGetKnownFolderPath(FOLDERID_Documents, KF_FLAG_DEFAULT, nullptr, &rawPath)) || !rawPath)
        {
            return {};
        }

        std::filesystem::path docsPath = rawPath;
        CoTaskMemFree(rawPath);
        
        auto result = docsPath / L"Frontier" / L"logs";
        return result.string();
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
    
    // Initialize shared memory with empty overlay state to clear any stale data from previous sessions
    overlay::OverlayState initialState;
    initialState.version = overlay::schema_version;
    initialState.generated_at_ms = now_ms();
    initialState.heartbeat_ms = initialState.generated_at_ms;
    initialState.source_online = false;  // No data from web app yet
    initialState.follow_mode_enabled = false;
    // route vector is empty by default
    const auto initialJson = overlay::serialize_overlay_state(initialState);
    const auto initialSerialized = initialJson.dump();
    sharedMemoryWriter_.write(initialSerialized, initialState.version, initialState.generated_at_ms);
    spdlog::info("Helper initialized with empty overlay state (cleared stale data)");
}

void HelperServer::setTelemetrySummaryHandler(TelemetrySummaryHandler handler)
{
    telemetrySummaryHandler_ = std::move(handler);
}

void HelperServer::setTelemetryResetHandler(TelemetryResetHandler handler)
{
    telemetryResetHandler_ = std::move(handler);
}

void HelperServer::setInjectOverlayHandler(InjectOverlayHandler handler)
{
    injectOverlayHandler_ = std::move(handler);
}

void HelperServer::setFollowModeProvider(FollowModeProvider provider)
{
    followModeProvider_ = std::move(provider);
}

void HelperServer::setFollowModeUpdateHandler(FollowModeUpdateHandler handler)
{
    followModeUpdateHandler_ = std::move(handler);
}

void HelperServer::setSessionTrackerProvider(SessionTrackerProvider provider)
{
    sessionTrackerProvider_ = std::move(provider);
}

void HelperServer::setLogPathReloadHandler(LogPathReloadHandler handler)
{
    logPathReloadHandler_ = std::move(handler);
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

bool HelperServer::updateTrackingFlag(bool enabled)
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

        latestOverlayStateJson_["visited_systems_tracking_enabled"] = enabled;
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
        spdlog::warn("Failed to publish tracking update to shared memory");
    }

    if (websocketHub_)
    {
        nlohmann::json envelope{{"type", "overlay_state"}, {"state", json}};
        websocketHub_->broadcastOverlayState(std::move(envelope));
        spdlog::info("updateTrackingFlag: Broadcasting tracking={} via WebSocket", enabled);
    }
    else
    {
        spdlog::warn("updateTrackingFlag: No WebSocket hub available for broadcast");
    }

    return true;
}

bool HelperServer::updateSessionState(bool hasActiveSession, std::optional<std::string> sessionId)
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

        latestOverlayStateJson_["has_active_session"] = hasActiveSession;
        if (sessionId.has_value())
        {
            latestOverlayStateJson_["active_session_id"] = *sessionId;
        }
        else
        {
            latestOverlayStateJson_["active_session_id"] = nullptr;
        }
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
        spdlog::warn("Failed to publish session update to shared memory");
    }

    if (websocketHub_)
    {
        nlohmann::json envelope{{"type", "overlay_state"}, {"state", json}};
        websocketHub_->broadcastOverlayState(std::move(envelope));
        spdlog::info("updateSessionState: Broadcasting hasActive={} sessionId={} via WebSocket", 
                     hasActiveSession, sessionId.value_or("null"));
    }
    else
    {
        spdlog::warn("updateSessionState: No WebSocket hub available for broadcast");
    }

    return true;
}

void HelperServer::broadcastWebSocketMessage(const nlohmann::json& message)
{
    if (websocketHub_)
    {
        websocketHub_->broadcastJson(message);
    }
    else
    {
        spdlog::warn("Cannot broadcast WebSocket message: WebSocket hub not initialized");
    }
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

    // Populate session tracking state from session tracker
    if (sessionTrackerProvider_)
    {
        auto* tracker = sessionTrackerProvider_();
        if (tracker)
        {
            enriched.visited_systems_tracking_enabled = tracker->isAllTimeTrackingEnabled();
            enriched.has_active_session = tracker->hasActiveSession();
            enriched.active_session_id = tracker->getActiveSessionId();
        }
    }

    // If this update is from the web app (HTTP POST), preserve player_marker from log watcher
    if (source == "http")
    {
        std::lock_guard<std::mutex> guard(overlayStateMutex_);
        if (!latestOverlayStateJson_.empty())
        {
            // Preserve player_marker from log watcher (authoritative for player position)
            if (latestOverlayStateJson_.contains("player_marker") && latestOverlayStateJson_["player_marker"].is_object())
            {
                const auto& marker = latestOverlayStateJson_["player_marker"];
                overlay::PlayerMarker preservedMarker;
                if (marker.contains("system_id")) preservedMarker.system_id = marker["system_id"].get<std::string>();
                if (marker.contains("display_name")) preservedMarker.display_name = marker["display_name"].get<std::string>();
                if (marker.contains("is_docked")) preservedMarker.is_docked = marker["is_docked"].get<bool>();
                enriched.player_marker = preservedMarker;
                spdlog::debug("Preserved player_marker from log watcher: {} ({})", 
                    preservedMarker.display_name, preservedMarker.system_id);
            }
        }
    }

    // If this update is from the log watcher (follow mode), preserve route data from web app
    if (source == "log-watcher")
    {
        std::lock_guard<std::mutex> guard(overlayStateMutex_);
        spdlog::debug("Log watcher update: checking for route to preserve (latestOverlayStateJson_.empty={}, has route={}, route size={})",
            latestOverlayStateJson_.empty(),
            latestOverlayStateJson_.contains("route"),
            latestOverlayStateJson_.contains("route") && latestOverlayStateJson_["route"].is_array() ? latestOverlayStateJson_["route"].size() : 0);
        
        if (!latestOverlayStateJson_.empty())
        {
            // Preserve authenticated/tribe state from web app (authoritative for auth)
            if (latestOverlayStateJson_.contains("authenticated"))
            {
                enriched.authenticated = latestOverlayStateJson_["authenticated"].get<bool>();
            }
            if (latestOverlayStateJson_.contains("tribe_id") && latestOverlayStateJson_["tribe_id"].is_string())
            {
                enriched.tribe_id = latestOverlayStateJson_["tribe_id"].get<std::string>();
            }
            if (latestOverlayStateJson_.contains("tribe_name") && latestOverlayStateJson_["tribe_name"].is_string())
            {
                enriched.tribe_name = latestOverlayStateJson_["tribe_name"].get<std::string>();
            }
            
            // Preserve pscan_data from existing state (web app is authoritative)
            if (latestOverlayStateJson_.contains("pscan_data") && latestOverlayStateJson_["pscan_data"].is_object())
            {
                spdlog::debug("Preserving pscan_data from web app");
                
                overlay::PscanData pscanData;
                const auto& pscanJson = latestOverlayStateJson_["pscan_data"];
                
                if (pscanJson.contains("system_id")) pscanData.system_id = pscanJson["system_id"].get<std::string>();
                if (pscanJson.contains("system_name")) pscanData.system_name = pscanJson["system_name"].get<std::string>();
                if (pscanJson.contains("scanned_at_ms")) pscanData.scanned_at_ms = pscanJson["scanned_at_ms"].get<std::uint64_t>();
                
                if (pscanJson.contains("nodes") && pscanJson["nodes"].is_array())
                {
                    for (const auto& nodeJson : pscanJson["nodes"])
                    {
                        overlay::PscanNode node;
                        if (nodeJson.contains("id")) node.id = nodeJson["id"].get<std::string>();
                        if (nodeJson.contains("name")) node.name = nodeJson["name"].get<std::string>();
                        if (nodeJson.contains("type")) node.type = nodeJson["type"].get<std::string>();
                        if (nodeJson.contains("owner_name")) node.owner_name = nodeJson["owner_name"].get<std::string>();
                        if (nodeJson.contains("distance_m")) node.distance_m = nodeJson["distance_m"].get<double>();
                        pscanData.nodes.push_back(node);
                    }
                }
                
                enriched.pscan_data = pscanData;
            }
            
            // Preserve route and active_route_node_id from existing state (web app is authoritative)
            if (latestOverlayStateJson_.contains("route") && latestOverlayStateJson_["route"].is_array())
            {
                const size_t routeSize = latestOverlayStateJson_["route"].size();
                
                if (routeSize > 1)
                {
                    // Web app has sent a multi-hop route; preserve it
                    // Only update player_marker and telemetry from log watcher
                    spdlog::debug("Preserving multi-hop route ({} hops) from web app", routeSize);
                
                // Deserialize route from existing JSON
                std::vector<overlay::RouteNode> existingRoute;
                for (const auto& nodeJson : latestOverlayStateJson_["route"])
                {
                    overlay::RouteNode node;
                    if (nodeJson.contains("system_id")) node.system_id = nodeJson["system_id"].get<std::string>();
                    if (nodeJson.contains("display_name")) node.display_name = nodeJson["display_name"].get<std::string>();
                    if (nodeJson.contains("distance_ly")) node.distance_ly = nodeJson["distance_ly"].get<double>();
                    if (nodeJson.contains("via_gate")) node.via_gate = nodeJson["via_gate"].get<bool>();
                    if (nodeJson.contains("via_smart_gate")) node.via_smart_gate = nodeJson["via_smart_gate"].get<bool>();
                    if (nodeJson.contains("planet_count")) node.planet_count = nodeJson["planet_count"].get<int>();
                    if (nodeJson.contains("network_nodes")) node.network_nodes = nodeJson["network_nodes"].get<int>();
                    if (nodeJson.contains("route_position")) node.route_position = nodeJson["route_position"].get<int>();
                    if (nodeJson.contains("total_route_hops")) node.total_route_hops = nodeJson["total_route_hops"].get<int>();
                    existingRoute.push_back(node);
                }
                enriched.route = existingRoute;
                
                // Preserve active_route_node_id
                if (latestOverlayStateJson_.contains("active_route_node_id"))
                {
                    enriched.active_route_node_id = latestOverlayStateJson_["active_route_node_id"].get<std::string>();
                }
                }
                else
                {
                    // Web app sent empty route (no route calculated) or single-system route (invalid)
                    // Explicitly clear route to prevent showing stale data
                    // BUT: preserve player_marker from log watcher (it's in the incoming 'enriched' state)
                    spdlog::debug("Clearing route (web app sent empty/single-system route, size={})", routeSize);
                    enriched.route.clear();
                    enriched.active_route_node_id.reset();
                    // player_marker is NOT cleared - it comes from log watcher and should persist
                }
            }
        }
    }

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

    spdlog::debug("Writing to shared memory: route size={}, source={}", enriched.route.size(), source);
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
        {"Access-Control-Allow-Headers", "Content-Type, X-EF-Helper-Auth, x-ef-overlay-token"},
        {"Access-Control-Allow-Methods", "GET, POST, OPTIONS"}
    });

    server_.Options(".*", [](const httplib::Request&, httplib::Response& res) {
        res.status = 204;
        // Default headers already set by set_default_headers above
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

    // GET /settings/logs - Get current log base path (custom or default)
    server_.Get("/settings/logs", [this](const httplib::Request& req, httplib::Response& res) {
        if (!authorize(req, res))
        {
            return;
        }

        std::string customPath = loadCustomLogBasePath();
        std::string effectivePath = customPath.empty() ? getDefaultLogBasePath() : customPath;
        bool isCustom = !customPath.empty();

        nlohmann::json payload{
            {"status", "ok"},
            {"base_path", effectivePath},
            {"is_custom", isCustom},
            {"chat_logs_path", effectivePath + "\\ChatLogs"},
            {"game_logs_path", effectivePath + "\\GameLogs"}
        };

        res.set_content(payload.dump(), application_json);
        res.status = 200;
    });

    // POST /settings/logs - Set custom log base path
    server_.Post("/settings/logs", [this](const httplib::Request& req, httplib::Response& res) {
        if (!authorize(req, res))
        {
            return;
        }

        auto json = nlohmann::json::parse(req.body, nullptr, false);
        if (json.is_discarded())
        {
            res.set_content(make_error("Invalid JSON").dump(), application_json);
            res.status = 400;
            return;
        }

        if (!json.contains("base_path") || !json["base_path"].is_string())
        {
            res.set_content(make_error("base_path (string) required").dump(), application_json);
            res.status = 400;
            return;
        }

        std::string basePath = json["base_path"].get<std::string>();

        // Validate path exists if non-empty
        if (!basePath.empty())
        {
            std::filesystem::path fsPath(basePath);
            if (!std::filesystem::exists(fsPath))
            {
                res.set_content(make_error("Path does not exist").dump(), application_json);
                res.status = 400;
                return;
            }
        }

        if (!saveCustomLogBasePath(basePath))
        {
            res.set_content(make_error("Failed to save log path setting").dump(), application_json);
            res.status = 500;
            return;
        }

        // Trigger log watcher reload if handler is set
        if (logPathReloadHandler_)
        {
            spdlog::info("Reloading log watcher with new base path: {}", basePath.empty() ? "(default)" : basePath);
            logPathReloadHandler_();
        }

        nlohmann::json payload{
            {"status", "ok"},
            {"message", basePath.empty() ? "Reset to default log path" : "Custom log path saved"}
        };

        res.set_content(payload.dump(), application_json);
        res.status = 200;
    });

    // DELETE /settings/logs - Reset to default log path
    server_.Delete("/settings/logs", [this](const httplib::Request& req, httplib::Response& res) {
        if (!authorize(req, res))
        {
            return;
        }

        if (!saveCustomLogBasePath(""))  // Empty string = delete registry key
        {
            res.set_content(make_error("Failed to reset log path setting").dump(), application_json);
            res.status = 500;
            return;
        }

        // Trigger log watcher reload
        if (logPathReloadHandler_)
        {
            spdlog::info("Resetting log watcher to default path");
            logPathReloadHandler_();
        }

        nlohmann::json payload{
            {"status", "ok"},
            {"message", "Log path reset to default"}
        };

        res.set_content(payload.dump(), application_json);
        res.status = 200;
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

    server_.Post("/inject", [this](const httplib::Request& req, httplib::Response& res) {
        if (!authorize(req, res))
        {
            return;
        }

        if (!injectOverlayHandler_)
        {
            res.set_content(make_error("Overlay injection unavailable").dump(), application_json);
            res.status = 503;
            return;
        }

        // Call the injection handler (returns true on success)
        const bool success = injectOverlayHandler_();
        
        if (success)
        {
            nlohmann::json payload{
                {"status", "ok"},
                {"message", "Overlay injection started successfully"}
            };
            res.set_content(payload.dump(), application_json);
            res.status = 200;
        }
        else
        {
            res.set_content(make_error("Overlay injection failed").dump(), application_json);
            res.status = 500;
        }
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
            spdlog::info("[POST /overlay/state] Parsed state: authenticated={}, tribe_id={}, tribe_name={}", 
                        state.authenticated, 
                        state.tribe_id.has_value() ? *state.tribe_id : "<none>",
                        state.tribe_name.has_value() ? *state.tribe_name : "<none>");
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

    // Session tracking endpoints
    server_.Get("/session/visited-systems", [this](const httplib::Request& req, httplib::Response& res) {
        if (!authorize(req, res))
        {
            return;
        }

        if (!sessionTrackerProvider_)
        {
            res.set_content(make_error("Session tracker unavailable").dump(), application_json);
            res.status = 503;
            return;
        }

        auto* tracker = sessionTrackerProvider_();
        if (!tracker)
        {
            res.set_content(make_error("Session tracker unavailable").dump(), application_json);
            res.status = 503;
            return;
        }

        const auto type = req.has_param("type") ? req.get_param_value("type") : "all";

        try
        {
            nlohmann::json payload;

            if (type == "all")
            {
                auto data = tracker->getAllTimeData();
                payload["version"] = data.version;
                payload["tracking_enabled"] = data.tracking_enabled;
                payload["last_updated_ms"] = data.last_updated_ms;
                
                nlohmann::json systems_obj = nlohmann::json::object();
                for (const auto& [system_id, sys_data] : data.systems)
                {
                    systems_obj[system_id] = {
                        {"name", sys_data.name},
                        {"visits", sys_data.visits}
                    };
                }
                payload["systems"] = systems_obj;
            }
            else if (type == "session")
            {
                if (!req.has_param("session_id"))
                {
                    res.set_content(make_error("session_id parameter required for type=session").dump(), application_json);
                    res.status = 400;
                    return;
                }
                const auto sessionId = req.get_param_value("session_id");
                auto maybeSession = tracker->getSessionData(sessionId);
                if (!maybeSession.has_value())
                {
                    res.set_content(make_error("Session not found").dump(), application_json);
                    res.status = 404;
                    return;
                }
                
                auto& data = *maybeSession;
                payload["version"] = data.version;
                payload["session_id"] = data.session_id;
                payload["start_time_ms"] = data.start_time_ms;
                payload["end_time_ms"] = data.end_time_ms;
                payload["active"] = data.active;
                
                nlohmann::json systems_obj = nlohmann::json::object();
                for (const auto& [system_id, sys_data] : data.systems)
                {
                    systems_obj[system_id] = {
                        {"name", sys_data.name},
                        {"visits", sys_data.visits}
                    };
                }
                payload["systems"] = systems_obj;
            }
            else if (type == "active-session")
            {
                auto maybeSession = tracker->getActiveSessionData();
                if (!maybeSession.has_value())
                {
                    res.set_content(make_error("No active session").dump(), application_json);
                    res.status = 404;
                    return;
                }
                
                auto& data = *maybeSession;
                payload["version"] = data.version;
                payload["session_id"] = data.session_id;
                payload["start_time_ms"] = data.start_time_ms;
                payload["end_time_ms"] = data.end_time_ms;
                payload["active"] = data.active;
                
                nlohmann::json systems_obj = nlohmann::json::object();
                for (const auto& [system_id, sys_data] : data.systems)
                {
                    systems_obj[system_id] = {
                        {"name", sys_data.name},
                        {"visits", sys_data.visits}
                    };
                }
                payload["systems"] = systems_obj;
            }
            else
            {
                res.set_content(make_error("Invalid type parameter (must be 'all', 'session', or 'active-session')").dump(), application_json);
                res.status = 400;
                return;
            }

            res.set_content(payload.dump(), application_json);
            res.status = 200;
        }
        catch (const std::exception& ex)
        {
            res.set_content(make_error(ex.what()).dump(), application_json);
            res.status = 500;
        }
    });

    server_.Post("/session/visited-systems/reset-all", [this](const httplib::Request& req, httplib::Response& res) {
        if (!authorize(req, res))
        {
            return;
        }

        if (!sessionTrackerProvider_)
        {
            res.set_content(make_error("Session tracker unavailable").dump(), application_json);
            res.status = 503;
            return;
        }

        auto* tracker = sessionTrackerProvider_();
        if (!tracker)
        {
            res.set_content(make_error("Session tracker unavailable").dump(), application_json);
            res.status = 503;
            return;
        }

        tracker->resetAllTimeTracking();
        nlohmann::json payload{{"status", "ok"}, {"message", "All-time tracking reset"}};
        res.set_content(payload.dump(), application_json);
        res.status = 200;
    });

    server_.Post("/session/start-session", [this](const httplib::Request& req, httplib::Response& res) {
        if (!authorize(req, res))
        {
            return;
        }

        if (!sessionTrackerProvider_)
        {
            res.set_content(make_error("Session tracker unavailable").dump(), application_json);
            res.status = 503;
            return;
        }

        auto* tracker = sessionTrackerProvider_();
        if (!tracker)
        {
            res.set_content(make_error("Session tracker unavailable").dump(), application_json);
            res.status = 503;
            return;
        }

        const auto sessionId = tracker->startSession();
        
        // Direct state update for instant overlay sync
        updateSessionState(true, sessionId);
        
        nlohmann::json payload{{"status", "ok"}, {"session_id", sessionId}};
        res.set_content(payload.dump(), application_json);
        res.status = 200;
    });

    server_.Post("/session/stop-session", [this](const httplib::Request& req, httplib::Response& res) {
        if (!authorize(req, res))
        {
            return;
        }

        if (!sessionTrackerProvider_)
        {
            res.set_content(make_error("Session tracker unavailable").dump(), application_json);
            res.status = 503;
            return;
        }

        auto* tracker = sessionTrackerProvider_();
        if (!tracker)
        {
            res.set_content(make_error("Session tracker unavailable").dump(), application_json);
            res.status = 503;
            return;
        }

        if (!tracker->hasActiveSession())
        {
            res.set_content(make_error("No active session").dump(), application_json);
            res.status = 404;
            return;
        }

        tracker->stopSession();
        
        // Direct state update for instant overlay sync
        updateSessionState(false, std::nullopt);
        
        nlohmann::json payload{{"status", "ok"}, {"message", "Session stopped"}};
        res.set_content(payload.dump(), application_json);
        res.status = 200;
    });

    server_.Post("/session/reset-session", [this](const httplib::Request& req, httplib::Response& res) {
        if (!authorize(req, res))
        {
            return;
        }

        if (!sessionTrackerProvider_)
        {
            res.set_content(make_error("Session tracker unavailable").dump(), application_json);
            res.status = 503;
            return;
        }

        auto* tracker = sessionTrackerProvider_();
        if (!tracker)
        {
            res.set_content(make_error("Session tracker unavailable").dump(), application_json);
            res.status = 503;
            return;
        }

        if (!tracker->hasActiveSession())
        {
            res.set_content(make_error("No active session to reset").dump(), application_json);
            res.status = 404;
            return;
        }

        tracker->resetActiveSession();
        nlohmann::json payload{{"status", "ok"}, {"message", "Active session reset"}};
        res.set_content(payload.dump(), application_json);
        res.status = 200;
    });

    server_.Get("/session/list-sessions", [this](const httplib::Request& req, httplib::Response& res) {
        if (!authorize(req, res))
        {
            return;
        }

        if (!sessionTrackerProvider_)
        {
            res.set_content(make_error("Session tracker unavailable").dump(), application_json);
            res.status = 503;
            return;
        }

        auto* tracker = sessionTrackerProvider_();
        if (!tracker)
        {
            res.set_content(make_error("Session tracker unavailable").dump(), application_json);
            res.status = 503;
            return;
        }

        try
        {
            auto sessions = tracker->listStoppedSessions();
            nlohmann::json sessions_array = nlohmann::json::array();
            
            for (const auto& session : sessions)
            {
                nlohmann::json session_obj;
                session_obj["session_id"] = session.session_id;
                session_obj["start_time_ms"] = session.start_time_ms;
                session_obj["end_time_ms"] = session.end_time_ms;
                session_obj["system_count"] = session.systems.size();
                sessions_array.push_back(session_obj);
            }

            nlohmann::json payload;
            payload["sessions"] = sessions_array;
            res.set_content(payload.dump(), application_json);
            res.status = 200;
        }
        catch (const std::exception& ex)
        {
            res.set_content(make_error(std::string("Failed to list sessions: ") + ex.what()).dump(), application_json);
            res.status = 500;
        }
    });

    server_.Post("/session/visited-systems/toggle", [this](const httplib::Request& req, httplib::Response& res) {
        if (!authorize(req, res))
        {
            return;
        }

        if (!sessionTrackerProvider_)
        {
            res.set_content(make_error("Session tracker unavailable").dump(), application_json);
            res.status = 503;
            return;
        }

        auto* tracker = sessionTrackerProvider_();
        if (!tracker)
        {
            res.set_content(make_error("Session tracker unavailable").dump(), application_json);
            res.status = 503;
            return;
        }

        // Support both auto-toggle (no body) and explicit set (body with {"enabled": bool})
        bool enabled;
        if (!req.body.empty())
        {
            auto json = nlohmann::json::parse(req.body, nullptr, false);
            if (json.is_discarded() || !json.contains("enabled") || !json["enabled"].is_boolean())
            {
                res.set_content(make_error("Request body must be JSON with 'enabled' boolean field").dump(), application_json);
                res.status = 400;
                return;
            }
            enabled = json["enabled"].get<bool>();
        }
        else
        {
            // Auto-toggle: flip current state
            enabled = !tracker->isAllTimeTrackingEnabled();
        }

        tracker->setAllTimeTrackingEnabled(enabled);
        
        // Direct state update for instant overlay sync
        updateTrackingFlag(enabled);
        
        nlohmann::json payload{{"status", "ok"}, {"enabled", enabled}};
        res.set_content(payload.dump(), application_json);
        res.status = 200;
    });
    
    // POST /bookmarks/create - Create a bookmark from overlay or web app
    server_.Post("/bookmarks/create", [this](const httplib::Request& req, httplib::Response& res) {
        if (!authorize(req, res))
        {
            return;
        }

        if (req.body.empty())
        {
            res.set_content(make_error("Request body required").dump(), application_json);
            res.status = 400;
            return;
        }

        auto json = nlohmann::json::parse(req.body, nullptr, false);
        if (json.is_discarded())
        {
            res.set_content(make_error("Invalid JSON").dump(), application_json);
            res.status = 400;
            return;
        }

        if (!json.contains("system_id") || !json["system_id"].is_string())
        {
            res.set_content(make_error("system_id (string) required").dump(), application_json);
            res.status = 400;
            return;
        }

        const std::string systemId = json["system_id"].get<std::string>();
        const std::string systemName = json.value("system_name", "");
        const std::string notes = json.value("notes", "");
        const bool forTribe = json.value("for_tribe", false);

        spdlog::info("Bookmark creation request: system={}, name={}, notes={}, for_tribe={}", 
                     systemId, systemName, notes, forTribe);

        // Extract auth state from latest overlay state (web app is authoritative for auth)
        bool authenticated = false;
        std::string tribeId;
        std::string tribeName;
        
        if (auto stateOpt = latestOverlayStateJson(); stateOpt.has_value())
        {
            const auto& stateJson = *stateOpt;
            authenticated = stateJson.value("authenticated", false);
            if (stateJson.contains("tribe_id") && stateJson["tribe_id"].is_string())
            {
                tribeId = stateJson["tribe_id"].get<std::string>();
            }
            if (stateJson.contains("tribe_name") && stateJson["tribe_name"].is_string())
            {
                tribeName = stateJson["tribe_name"].get<std::string>();
            }
            spdlog::info("Auth state from overlay: authenticated={}, tribe_id={}, tribe_name={}", 
                        authenticated, 
                        tribeId.empty() ? "<none>" : tribeId, 
                        tribeName.empty() ? "<none>" : tribeName);
        }

        // Decision: Personal (client-side) vs Tribe (server-side) storage
        // - Personal: User NOT authenticated OR for_tribe=false OR tribe=clonebank
        //   → Broadcast to web app to add to userOverlayStore (localStorage)
        // - Tribe: User IS authenticated AND for_tribe=true AND tribe!=clonebank
        //   → Broadcast to web app for tribe folder (will POST to /api/tribe-marks)
        
        const bool isCloneBank = (tribeName.find("Clonebank") != std::string::npos || 
                                  tribeName.find("clonebank") != std::string::npos ||
                                  tribeId == "98008314");  // CloneBank86 tribe ID
        const bool routeToTribe = authenticated && forTribe && !tribeId.empty() && !isCloneBank;
        
        spdlog::info("Routing decision: route_to_tribe={}, clonebank={}", routeToTribe, isCloneBank);
        
        // Broadcast bookmark creation request to web app via WebSocket
        // Web app handles both personal (userOverlayStore) and tribe (POST /api/tribe-marks) storage
        nlohmann::json wsMessage;
        wsMessage["type"] = "bookmark_add_request";
        wsMessage["payload"]["system_id"] = systemId;
        wsMessage["payload"]["system_name"] = systemName;
        wsMessage["payload"]["notes"] = notes;
        wsMessage["payload"]["for_tribe"] = routeToTribe;  // Use computed routing decision
        wsMessage["payload"]["color"] = "#ff4c26";  // Default orange color
        wsMessage["payload"]["tribe_id"] = routeToTribe ? tribeId : "";
        wsMessage["payload"]["tribe_name"] = routeToTribe ? tribeName : "";
        
        if (websocketHub_)
        {
            websocketHub_->broadcastJson(wsMessage);
            spdlog::info("Broadcast bookmark creation request to web app ({})", routeToTribe ? "tribe folder" : "personal folder");
        }
        else
        {
            spdlog::warn("No WebSocket hub available - bookmark not created");
        }
        
        nlohmann::json payload{{"status", "ok"}, {"system_id", systemId}, {"routed_to", routeToTribe ? "tribe" : "personal"}};
        res.set_content(payload.dump(), application_json);
        res.status = 200;
    });

    // GET /pscan/data - Retrieve latest proximity scan results
    server_.Get("/pscan/data", [this](const httplib::Request& req, httplib::Response& res) {
        if (!authorize(req, res))
        {
            return;
        }

        std::lock_guard<std::mutex> guard(pscanMutex_);
        
        if (!latestPscanData_.has_value())
        {
            res.set_content(make_error("No scan data available").dump(), application_json);
            res.status = 404;
            return;
        }

        const auto& pscan = *latestPscanData_;
        nlohmann::json nodes = nlohmann::json::array();
        for (const auto& node : pscan.nodes)
        {
            nodes.push_back({
                {"id", node.id},
                {"name", node.name},
                {"type", node.type},
                {"owner_name", node.owner_name},
                {"distance_m", node.distance_m}
            });
        }

        nlohmann::json payload{
            {"status", "ok"},
            {"system_id", pscan.system_id},
            {"system_name", pscan.system_name},
            {"scanned_at_ms", pscan.scanned_at_ms},
            {"nodes", std::move(nodes)}
        };

        res.set_content(payload.dump(), application_json);
        res.status = 200;
    });

    // POST /pscan/data - Store new proximity scan results
    server_.Post("/pscan/data", [this](const httplib::Request& req, httplib::Response& res) {
        if (!authorize(req, res))
        {
            return;
        }

        if (req.body.empty())
        {
            res.set_content(make_error("Request body required").dump(), application_json);
            res.status = 400;
            return;
        }

        auto json = nlohmann::json::parse(req.body, nullptr, false);
        if (json.is_discarded())
        {
            res.set_content(make_error("Invalid JSON").dump(), application_json);
            res.status = 400;
            return;
        }

        if (!json.contains("system_id") || !json["system_id"].is_string())
        {
            res.set_content(make_error("system_id (string) required").dump(), application_json);
            res.status = 400;
            return;
        }

        overlay::PscanData pscan;
        pscan.system_id = json["system_id"].get<std::string>();
        pscan.system_name = json.value("system_name", "");
        pscan.scanned_at_ms = json.value("scanned_at_ms", now_ms());

        if (json.contains("nodes") && json["nodes"].is_array())
        {
            for (const auto& nodeJson : json["nodes"])
            {
                if (!nodeJson.is_object())
                {
                    continue;
                }

                overlay::PscanNode node;
                node.id = nodeJson.value("id", "");
                node.name = nodeJson.value("name", "");
                node.type = nodeJson.value("type", "");
                node.owner_name = nodeJson.value("owner_name", "");
                node.distance_m = nodeJson.value("distance_m", 0.0);

                pscan.nodes.push_back(std::move(node));
            }
        }

        spdlog::info("P-SCAN data received: system={}, nodes={}", pscan.system_id, pscan.nodes.size());

        // Store in helper state
        {
            std::lock_guard<std::mutex> guard(pscanMutex_);
            latestPscanData_ = pscan;
        }

        // Update overlay state with PSCAN data
        {
            std::lock_guard<std::mutex> guard(overlayStateMutex_);
            if (hasOverlayState_.load() && !latestOverlayState_.empty())
            {
                latestOverlayStateJson_["pscan_data"] = {
                    {"system_id", pscan.system_id},
                    {"system_name", pscan.system_name},
                    {"scanned_at_ms", pscan.scanned_at_ms}
                };

                nlohmann::json nodesArray = nlohmann::json::array();
                for (const auto& node : pscan.nodes)
                {
                    nodesArray.push_back({
                        {"id", node.id},
                        {"name", node.name},
                        {"type", node.type},
                        {"owner_name", node.owner_name},
                        {"distance_m", node.distance_m}
                    });
                }
                latestOverlayStateJson_["pscan_data"]["nodes"] = std::move(nodesArray);

                latestOverlayStateJson_["heartbeat_ms"] = now_ms();
                const auto serialized = latestOverlayStateJson_.dump();
                latestOverlayState_ = serialized;

                const std::uint32_t version = latestOverlayStateJson_.value("version", overlay::schema_version);
                const std::uint64_t generatedAt = latestOverlayStateJson_.value("generated_at_ms", 0ULL);

                sharedMemoryWriter_.write(serialized, version, generatedAt);
                
                if (websocketHub_)
                {
                    nlohmann::json envelope{{"type", "overlay_state"}, {"state", latestOverlayStateJson_}};
                    websocketHub_->broadcastOverlayState(std::move(envelope));
                }

                spdlog::info("P-SCAN data pushed to overlay (shared memory + WebSocket)");
            }
        }

        nlohmann::json payload{{"status", "ok"}, {"nodes_received", pscan.nodes.size()}};
        res.set_content(payload.dump(), application_json);
        res.status = 200;
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
