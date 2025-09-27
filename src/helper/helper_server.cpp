#include "helper_server.hpp"

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <sstream>
#include <utility>

namespace
{
    constexpr const char* application_json = "application/json";
    constexpr const char* text_plain = "text/plain";

    nlohmann::json make_error(std::string_view message)
    {
        return nlohmann::json{{"status", "error"}, {"message", message}};
    }
}

HelperServer::HelperServer(std::string host, int port, std::string authToken)
    : host_(std::move(host))
    , port_(port)
    , authToken_(std::move(authToken))
    , requireAuth_(!authToken_.empty())
{
    configureRoutes();
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

    running_.store(true);
    startedAt_ = std::chrono::steady_clock::now();
    stoppedAt_ = startedAt_;

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
    const auto serialized = overlay::serialize_overlay_state(state).dump();

    {
        std::lock_guard<std::mutex> guard(overlayStateMutex_);
        latestOverlayState_ = serialized;
    }

    hasOverlayState_.store(true);

    const bool sharedOk = sharedMemoryWriter_.write(serialized, static_cast<std::uint32_t>(state.version), state.generated_at_ms);
    if (!sharedOk)
    {
        spdlog::warn("Overlay state accepted via {} but failed to publish to shared memory", source);
    }

    spdlog::info("Overlay state accepted via {} ({} bytes)", std::move(source), static_cast<unsigned long long>(requestBytes));
    return true;
}

void HelperServer::configureRoutes()
{
    using namespace std::chrono;

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
