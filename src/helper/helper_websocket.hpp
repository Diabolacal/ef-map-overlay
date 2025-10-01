#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <asio.hpp>
#include <nlohmann/json.hpp>

namespace helper::ws
{
    struct EventEnvelope
    {
        nlohmann::json payload;
    };

    class HelperWebSocketHub
    {
    public:
        struct Config
        {
            std::string host{"127.0.0.1"};
            int port{0};
            int httpPort{0};
            std::string token;
            std::function<std::optional<nlohmann::json>()> getLatestOverlayState;
        };

        explicit HelperWebSocketHub(Config config);
        ~HelperWebSocketHub();

        HelperWebSocketHub(const HelperWebSocketHub&) = delete;
        HelperWebSocketHub& operator=(const HelperWebSocketHub&) = delete;

        bool start();
        void stop();

    int port() const noexcept { return config_.port; }

    void broadcastJson(const nlohmann::json& message);
        void broadcastOverlayState(const nlohmann::json& state);
        void broadcastEventBatch(nlohmann::json batch);

    private:
        struct Client;

        void acceptLoop();
        bool performHandshake(Client& client, std::string& requestBuffer);
        void readerLoop(std::shared_ptr<Client> client);
        void sendInitialPayload(const std::shared_ptr<Client>& client);
    bool sendText(const std::shared_ptr<Client>& client, const std::string& text);
        static bool readExact(asio::ip::tcp::socket& socket, void* buffer, std::size_t length);

        Config config_;
        asio::io_context io_;
        asio::ip::tcp::acceptor acceptor_;
        std::thread acceptThread_;
        std::thread pingThread_;
        std::atomic_bool running_{false};

        std::mutex clientsMutex_;
        std::vector<std::weak_ptr<Client>> clients_;
    };
}
