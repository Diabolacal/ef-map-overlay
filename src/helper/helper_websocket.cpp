#include "helper_websocket.hpp"

#include <bcrypt.h>
#include <windows.h>

#include <spdlog/spdlog.h>
#include <httplib.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <sstream>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace
{
    constexpr const char* websocket_guid = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

    std::string sha1_base64(const std::string& value)
    {
        BCRYPT_ALG_HANDLE algorithm = nullptr;
        BCRYPT_HASH_HANDLE hash = nullptr;
        DWORD objectLength = 0;
        DWORD hashLength = 0;
        DWORD cbData = 0;

        NTSTATUS status = BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA1_ALGORITHM, nullptr, 0);
        if (!BCRYPT_SUCCESS(status))
        {
            return {};
        }

        status = BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&objectLength), sizeof(DWORD), &cbData, 0);
        if (!BCRYPT_SUCCESS(status))
        {
            BCryptCloseAlgorithmProvider(algorithm, 0);
            return {};
        }

        status = BCryptGetProperty(algorithm, BCRYPT_HASH_LENGTH, reinterpret_cast<PUCHAR>(&hashLength), sizeof(DWORD), &cbData, 0);
        if (!BCRYPT_SUCCESS(status))
        {
            BCryptCloseAlgorithmProvider(algorithm, 0);
            return {};
        }

        std::vector<UCHAR> hashObject(objectLength);
        std::vector<UCHAR> hashBuffer(hashLength);

        status = BCryptCreateHash(algorithm, &hash, hashObject.data(), objectLength, nullptr, 0, 0);
        if (!BCRYPT_SUCCESS(status))
        {
            BCryptCloseAlgorithmProvider(algorithm, 0);
            return {};
        }

    status = BCryptHashData(hash, reinterpret_cast<PUCHAR>(const_cast<char*>(value.data())), static_cast<ULONG>(value.size()), 0);
        if (BCRYPT_SUCCESS(status))
        {
            status = BCryptFinishHash(hash, hashBuffer.data(), hashLength, 0);
        }

        BCryptDestroyHash(hash);
        BCryptCloseAlgorithmProvider(algorithm, 0);

        if (!BCRYPT_SUCCESS(status))
        {
            return {};
        }

    const std::string hashString(reinterpret_cast<const char*>(hashBuffer.data()), hashBuffer.size());
    return httplib::detail::base64_encode(hashString);
    }

    std::string to_lower(std::string value)
    {
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        return value;
    }

    std::unordered_map<std::string, std::string> parse_query_params(const std::string& query)
    {
        std::unordered_map<std::string, std::string> result;
        std::size_t start = 0;
        while (start < query.size())
        {
            std::size_t end = query.find('&', start);
            if (end == std::string::npos)
            {
                end = query.size();
            }
            const std::string_view part(query.data() + start, end - start);
            if (!part.empty())
            {
                const auto eq = part.find('=');
                if (eq == std::string_view::npos)
                {
                    result.emplace(std::string(part), std::string{});
                }
                else
                {
                    result.emplace(std::string(part.substr(0, eq)), std::string(part.substr(eq + 1)));
                }
            }
            start = end + 1;
        }
        return result;
    }

    std::string make_http_error_response(int status, const std::string& message)
    {
        nlohmann::json payload{{"status", "error"}, {"message", message}};
        const auto body = payload.dump();

        std::ostringstream oss;
        oss << "HTTP/1.1 " << status << " Error\r\n";
        oss << "Content-Type: application/json\r\n";
        oss << "Content-Length: " << body.size() << "\r\n";
        oss << "Connection: close\r\n\r\n";
        oss << body;
        return oss.str();
    }

    std::string make_trimmed(const std::string& value)
    {
        auto begin = value.find_first_not_of(" \t\r\n");
        auto end = value.find_last_not_of(" \t\r\n");
        if (begin == std::string::npos)
        {
            return {};
        }
        return value.substr(begin, end - begin + 1);
    }
}

namespace helper::ws
{
    struct HelperWebSocketHub::Client
    {
        explicit Client(asio::ip::tcp::socket socket)
            : socket(std::move(socket))
        {
        }

        ~Client()
        {
            // Signal thread to exit
            running.store(false);
            
            // Close socket to unblock any pending reads
            asio::error_code ec;
            socket.shutdown(asio::ip::tcp::socket::shutdown_both, ec);
            socket.close(ec);
            
            // Wait for thread to exit (with timeout)
            if (readerThread.joinable())
            {
                // Give thread a moment to exit gracefully
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                
                // If still joinable, just detach to avoid blocking destructor
                if (readerThread.joinable())
                {
                    readerThread.detach();
                }
            }
        }

        asio::ip::tcp::socket socket;
        std::mutex writeMutex;
        std::thread readerThread;
        std::atomic_bool running{true};
        std::string remoteAddress;
    };

    HelperWebSocketHub::HelperWebSocketHub(Config config)
        : config_(std::move(config))
        , io_()
        , acceptor_(io_)
    {
    }

    HelperWebSocketHub::~HelperWebSocketHub()
    {
        stop();
    }

    bool HelperWebSocketHub::start()
    {
        if (running_.exchange(true))
        {
            return true;
        }

        asio::error_code ec;
        const auto address = asio::ip::make_address(config_.host, ec);
        if (ec)
        {
            spdlog::error("[ws] invalid host {}: {}", config_.host, ec.message());
            running_.store(false);
            return false;
        }

        asio::ip::tcp::endpoint endpoint(address, static_cast<unsigned short>(config_.port));

        acceptor_.open(endpoint.protocol(), ec);
        if (ec)
        {
            spdlog::error("[ws] failed to open acceptor: {}", ec.message());
            running_.store(false);
            return false;
        }

        acceptor_.set_option(asio::ip::tcp::acceptor::reuse_address(true), ec);
        if (ec)
        {
            spdlog::warn("[ws] reuse_address failed: {}", ec.message());
        }

        acceptor_.bind(endpoint, ec);
        if (ec)
        {
            spdlog::error("[ws] failed to bind {}:{} - {}", config_.host, config_.port, ec.message());
            acceptor_.close();
            running_.store(false);
            return false;
        }

        acceptor_.listen(asio::socket_base::max_listen_connections, ec);
        if (ec)
        {
            spdlog::error("[ws] listen failed: {}", ec.message());
            acceptor_.close();
            running_.store(false);
            return false;
        }

        // Update port if caller requested dynamic port 0
        config_.port = static_cast<int>(acceptor_.local_endpoint().port());

        acceptThread_ = std::thread([this]() {
            acceptLoop();
        });

        pingThread_ = std::thread([this]() {
            using namespace std::chrono_literals;
            while (running_.load())
            {
                std::this_thread::sleep_for(15s);
                if (!running_.load())
                {
                    break;
                }

                nlohmann::json ping{{"type", "ping"}, {"now_ms", std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count()}};
                broadcastJson(ping);
            }
        });

        spdlog::info("Helper WebSocket hub listening on {}:{}", config_.host, config_.port);
        return true;
    }

    void HelperWebSocketHub::stop()
    {
        if (!running_.exchange(false))
        {
            return;
        }

        asio::error_code ec;
        acceptor_.cancel(ec);
        acceptor_.close(ec);

        if (acceptThread_.joinable())
        {
            acceptThread_.join();
        }

        if (pingThread_.joinable())
        {
            pingThread_.join();
        }

        std::vector<std::shared_ptr<Client>> clients;
        {
            std::lock_guard<std::mutex> guard(clientsMutex_);
            for (auto& weak : clients_)
            {
                if (auto locked = weak.lock())
                {
                    clients.push_back(std::move(locked));
                }
            }
            clients_.clear();
        }

        for (auto& client : clients)
        {
            client->running.store(false);
            client->socket.close();
            if (client->readerThread.joinable())
            {
                client->readerThread.join();
            }
        }

        spdlog::info("Helper WebSocket hub stopped");
    }

    void HelperWebSocketHub::broadcastJson(const nlohmann::json& message)
    {
        const auto serialized = message.dump();

        std::lock_guard<std::mutex> guard(clientsMutex_);
        for (auto it = clients_.begin(); it != clients_.end();)
        {
            if (auto client = it->lock())
            {
                try
                {
                    if (!sendText(client, serialized))
                    {
                        client->running.store(false);
                        // Don't join here - let client cleanup happen naturally or in stop()
                        // Joining from broadcast thread can cause deadlock
                        it = clients_.erase(it);
                        continue;
                    }
                }
                catch (const std::exception& ex)
                {
                    spdlog::debug("[ws] broadcastJson exception for client {}: {}", client->remoteAddress, ex.what());
                    client->running.store(false);
                    // Don't join here - let client cleanup happen naturally or in stop()
                    it = clients_.erase(it);
                    continue;
                }
                ++it;
            }
            else
            {
                it = clients_.erase(it);
            }
        }
    }

    void HelperWebSocketHub::broadcastOverlayState(const nlohmann::json& state)
    {
        const auto serialized = state.dump();

        std::lock_guard<std::mutex> guard(clientsMutex_);
        for (auto it = clients_.begin(); it != clients_.end();)
        {
            if (auto client = it->lock())
            {
                try
                {
                    if (!sendText(client, serialized))
                    {
                        client->running.store(false);
                        // Don't join here - let client cleanup happen naturally or in stop()
                        it = clients_.erase(it);
                        continue;
                    }
                }
                catch (const std::exception& ex)
                {
                    spdlog::debug("[ws] broadcastOverlayState exception for client {}: {}", client->remoteAddress, ex.what());
                    client->running.store(false);
                    // Don't join here - let client cleanup happen naturally or in stop()
                    it = clients_.erase(it);
                    continue;
                }
                ++it;
            }
            else
            {
                it = clients_.erase(it);
            }
        }
    }

    void HelperWebSocketHub::broadcastEventBatch(nlohmann::json batch)
    {
        batch["type"] = "overlay_events";
        const auto serialized = batch.dump();

        std::lock_guard<std::mutex> guard(clientsMutex_);
        for (auto it = clients_.begin(); it != clients_.end();)
        {
            if (auto client = it->lock())
            {
                try
                {
                    if (!sendText(client, serialized))
                    {
                        client->running.store(false);
                        // Don't join here - let client cleanup happen naturally or in stop()
                        it = clients_.erase(it);
                        continue;
                    }
                }
                catch (const std::exception& ex)
                {
                    spdlog::debug("[ws] broadcastEventBatch exception for client {}: {}", client->remoteAddress, ex.what());
                    client->running.store(false);
                    // Don't join here - let client cleanup happen naturally or in stop()
                    it = clients_.erase(it);
                    continue;
                }
                ++it;
            }
            else
            {
                it = clients_.erase(it);
            }
        }
    }

    void HelperWebSocketHub::acceptLoop()
    {
        while (running_.load())
        {
            asio::error_code ec;
            asio::ip::tcp::socket socket(io_);
            acceptor_.accept(socket, ec);
            if (ec)
            {
                if (running_.load())
                {
                    spdlog::warn("[ws] accept failed: {}", ec.message());
                }
                continue;
            }

            auto client = std::make_shared<Client>(std::move(socket));
            client->remoteAddress = client->socket.remote_endpoint().address().to_string();

            std::string requestBuffer;
            if (!performHandshake(*client, requestBuffer))
            {
                client->socket.close();
                continue;
            }

            sendInitialPayload(client);

            client->readerThread = std::thread([this, client]() {
                readerLoop(client);
            });

            {
                std::lock_guard<std::mutex> guard(clientsMutex_);
                clients_.push_back(client);
            }
        }
    }

    bool HelperWebSocketHub::performHandshake(Client& client, std::string& buffer)
    {
        try
        {
            while (buffer.find("\r\n\r\n") == std::string::npos)
            {
                std::array<char, 512> temp{};
                const auto bytes = client.socket.read_some(asio::buffer(temp));
                if (bytes == 0)
                {
                    return false;
                }
                buffer.append(temp.data(), bytes);
                if (buffer.size() > 16384)
                {
                    return false;
                }
            }

            const auto headerEnd = buffer.find("\r\n\r\n");
            std::string headerPart = buffer.substr(0, headerEnd);

            std::istringstream stream(headerPart);
            std::string requestLine;
            if (!std::getline(stream, requestLine))
            {
                return false;
            }
            if (!requestLine.empty() && requestLine.back() == '\r')
            {
                requestLine.pop_back();
            }

            std::istringstream requestLineStream(requestLine);
            std::string method;
            std::string target;
            std::string version;
            requestLineStream >> method >> target >> version;
            if (method != "GET")
            {
                auto error = make_http_error_response(405, "Only GET supported for WebSocket handshake");
                asio::write(client.socket, asio::buffer(error));
                return false;
            }

            std::string path = target;
            std::string query;
            const auto queryPos = target.find('?');
            if (queryPos != std::string::npos)
            {
                path = target.substr(0, queryPos);
                query = target.substr(queryPos + 1);
            }

            if (path != "/overlay/stream")
            {
                auto error = make_http_error_response(404, "Unknown WebSocket endpoint");
                asio::write(client.socket, asio::buffer(error));
                return false;
            }

            std::unordered_map<std::string, std::string> headers;
            std::string line;
            while (std::getline(stream, line))
            {
                if (!line.empty() && line.back() == '\r')
                {
                    line.pop_back();
                }
                if (line.empty())
                {
                    continue;
                }
                const auto colon = line.find(':');
                if (colon == std::string::npos)
                {
                    continue;
                }
                std::string key = make_trimmed(line.substr(0, colon));
                std::string value = make_trimmed(line.substr(colon + 1));
                headers[to_lower(key)] = value;
            }

            const auto upgradeIt = headers.find("upgrade");
            const auto connectionIt = headers.find("connection");
            const auto keyIt = headers.find("sec-websocket-key");

            if (upgradeIt == headers.end() || to_lower(upgradeIt->second) != "websocket" ||
                connectionIt == headers.end() || to_lower(connectionIt->second).find("upgrade") == std::string::npos ||
                keyIt == headers.end())
            {
                auto error = make_http_error_response(400, "Missing required WebSocket headers");
                asio::write(client.socket, asio::buffer(error));
                return false;
            }

            const auto params = parse_query_params(query);

            if (!config_.token.empty())
            {
                std::string tokenCandidate;
                const auto headerToken = headers.find("x-ef-overlay-token");
                if (headerToken != headers.end())
                {
                    tokenCandidate = headerToken->second;
                }
                else
                {
                    const auto paramIt = params.find("token");
                    if (paramIt != params.end())
                    {
                        tokenCandidate = paramIt->second;
                    }
                }

                if (tokenCandidate != config_.token)
                {
                    auto error = make_http_error_response(401, "Unauthorized");
                    asio::write(client.socket, asio::buffer(error));
                    return false;
                }
            }

            const auto acceptKey = sha1_base64(keyIt->second + websocket_guid);
            if (acceptKey.empty())
            {
                auto error = make_http_error_response(500, "Unable to compute handshake");
                asio::write(client.socket, asio::buffer(error));
                return false;
            }

            std::ostringstream response;
            response << "HTTP/1.1 101 Switching Protocols\r\n";
            response << "Upgrade: websocket\r\n";
            response << "Connection: Upgrade\r\n";
            response << "Sec-WebSocket-Accept: " << acceptKey << "\r\n";
            response << "Sec-WebSocket-Version: 13\r\n";
            response << "Access-Control-Allow-Origin: *\r\n";
            response << "\r\n";

            const auto handshake = response.str();
            asio::write(client.socket, asio::buffer(handshake));
            return true;
        }
        catch (const std::exception& ex)
        {
            spdlog::warn("[ws] handshake exception: {}", ex.what());
            return false;
        }
    }

    void HelperWebSocketHub::readerLoop(std::shared_ptr<Client> client)
    {
        try
        {
            while (client->running.load())
            {
                std::array<unsigned char, 2> header{};
                if (!readExact(client->socket, header.data(), header.size()))
                {
                    break;
                }

                const bool fin = (header[0] & 0x80) != 0;
                const unsigned char opcode = header[0] & 0x0F;
                const bool masked = (header[1] & 0x80) != 0;
                std::uint64_t payloadLength = header[1] & 0x7F;

                if (!fin)
                {
                    spdlog::debug("[ws] fragmented frame from {} ignored", client->remoteAddress);
                    return;
                }

                if (payloadLength == 126)
                {
                    std::array<unsigned char, 2> extended{};
                    if (!readExact(client->socket, extended.data(), extended.size()))
                    {
                        break;
                    }
                    payloadLength = static_cast<std::uint64_t>(extended[0]) << 8 | static_cast<std::uint64_t>(extended[1]);
                }
                else if (payloadLength == 127)
                {
                    std::array<unsigned char, 8> extended{};
                    if (!readExact(client->socket, extended.data(), extended.size()))
                    {
                        break;
                    }
                    payloadLength = 0;
                    for (int i = 0; i < 8; ++i)
                    {
                        payloadLength = (payloadLength << 8) | extended[i];
                    }
                }

                std::array<unsigned char, 4> mask{};
                if (masked)
                {
                    if (!readExact(client->socket, mask.data(), mask.size()))
                    {
                        break;
                    }
                }

                std::string payload;
                payload.resize(static_cast<std::size_t>(payloadLength));
                if (payloadLength > 0)
                {
                    if (!readExact(client->socket, payload.data(), payload.size()))
                    {
                        break;
                    }
                    if (masked)
                    {
                        for (std::size_t i = 0; i < payload.size(); ++i)
                        {
                            payload[i] = static_cast<char>(payload[i] ^ mask[i % 4]);
                        }
                    }
                }

                if (opcode == 0x8)
                {
                    // close
                    break;
                }
                else if (opcode == 0x9)
                {
                    // ping -> respond with pong
                    std::vector<unsigned char> frame;
                    frame.reserve(payload.size() + 2 + 8);
                    frame.push_back(0x8A);
                    if (payload.size() <= 125)
                    {
                        frame.push_back(static_cast<unsigned char>(payload.size()));
                    }
                    else if (payload.size() <= 65535)
                    {
                        frame.push_back(126);
                        frame.push_back(static_cast<unsigned char>((payload.size() >> 8) & 0xFF));
                        frame.push_back(static_cast<unsigned char>(payload.size() & 0xFF));
                    }
                    else
                    {
                        frame.push_back(127);
                        std::uint64_t len = payload.size();
                        for (int i = 7; i >= 0; --i)
                        {
                            frame.push_back(static_cast<unsigned char>((len >> (i * 8)) & 0xFF));
                        }
                    }
                    frame.insert(frame.end(), payload.begin(), payload.end());
                    std::lock_guard<std::mutex> lock(client->writeMutex);
                    asio::write(client->socket, asio::buffer(frame));
                }
                else if (opcode == 0xA)
                {
                    // pong -> ignore
                    continue;
                }
                else
                {
                    // Text or binary payloads ignored for now
                    continue;
                }
            }
        }
        catch (const std::exception& ex)
        {
            spdlog::debug("[ws] client {} disconnected with exception: {}", client->remoteAddress, ex.what());
        }

        client->running.store(false);
        asio::error_code ec;
        client->socket.close(ec);
    }

    void HelperWebSocketHub::sendInitialPayload(const std::shared_ptr<Client>& client)
    {
        nlohmann::json hello{
            {"type", "hello"},
            {"version", 1},
            {"features", nlohmann::json::array({
                "overlay_state",
                "overlay_events",
                "follow_mode",
                "telemetry_v1",
                "mining_telemetry",
                "telemetry_reset"
            })},
            {"http_port", config_.httpPort},
            {"ws_port", config_.port}
        };
        sendText(client, hello.dump());

        if (config_.getLatestOverlayState)
        {
            if (auto state = config_.getLatestOverlayState())
            {
                nlohmann::json envelope{{"type", "overlay_state"}, {"state", *state}};
                sendText(client, envelope.dump());
            }
        }
    }

    bool HelperWebSocketHub::sendText(const std::shared_ptr<Client>& client, const std::string& text)
    {
        if (!client->running.load())
        {
            return false;
        }

        std::vector<unsigned char> frame;
        frame.reserve(text.size() + 10);
        frame.push_back(0x81);

        const auto len = text.size();
        if (len <= 125)
        {
            frame.push_back(static_cast<unsigned char>(len));
        }
        else if (len <= 65535)
        {
            frame.push_back(126);
            frame.push_back(static_cast<unsigned char>((len >> 8) & 0xFF));
            frame.push_back(static_cast<unsigned char>(len & 0xFF));
        }
        else
        {
            frame.push_back(127);
            std::uint64_t bigLen = static_cast<std::uint64_t>(len);
            for (int i = 7; i >= 0; --i)
            {
                frame.push_back(static_cast<unsigned char>((bigLen >> (i * 8)) & 0xFF));
            }
        }

        frame.insert(frame.end(), text.begin(), text.end());

        std::lock_guard<std::mutex> lock(client->writeMutex);
        asio::error_code ec;
        asio::write(client->socket, asio::buffer(frame), ec);
        if (ec)
        {
            spdlog::debug("[ws] failed to send to {}: {}", client->remoteAddress, ec.message());
            return false;
        }
        return true;
    }

    bool HelperWebSocketHub::readExact(asio::ip::tcp::socket& socket, void* buffer, std::size_t length)
    {
        asio::error_code ec;
        asio::read(socket, asio::buffer(buffer, length), asio::transfer_exactly(length), ec);
        return !ec;
    }

}
