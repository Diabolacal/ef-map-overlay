#include "event_channel.hpp"
#include "helper_runtime.hpp"
#include "overlay_schema.hpp"
#include "protocol_registration.hpp"

#include <windows.h>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>

#include <httplib.h>
#include <nlohmann/json.hpp>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

namespace
{
    constexpr int default_port = 38765;
    constexpr char helper_name[] = "ef-overlay-helper";
    constexpr wchar_t protocol_scheme[] = L"ef-overlay://";

    enum class Mode
    {
        RunServer,
        RegisterProtocol,
        UnregisterProtocol,
        HandleUri
    };

    struct ProgramOptions
    {
        Mode mode{Mode::RunServer};
        std::wstring uri;
    };

    struct UriCommand
    {
        std::string action;
        std::string token;
        std::optional<nlohmann::json> payload;
        std::string payloadSerialized;
        std::size_t payloadBytes{0};
    };

    std::atomic_bool& is_running()
    {
        static std::atomic_bool running{true};
        return running;
    }

    BOOL WINAPI console_ctrl_handler(DWORD signal)
    {
        switch (signal)
        {
            case CTRL_C_EVENT:
            case CTRL_CLOSE_EVENT:
            case CTRL_BREAK_EVENT:
                is_running() = false;
                return TRUE;
            default:
                return FALSE;
        }
    }

    std::string to_utf8(const wchar_t* value, const char* fallback)
    {
        if (value == nullptr)
        {
            return std::string{fallback};
        }

        const int size = ::WideCharToMultiByte(CP_UTF8, 0, value, -1, nullptr, 0, nullptr, nullptr);
        if (size <= 1)
        {
            return std::string{fallback};
        }

        std::string buffer(static_cast<std::size_t>(size - 1), '\0');
        ::WideCharToMultiByte(CP_UTF8, 0, value, -1, buffer.data(), size, nullptr, nullptr);
        return buffer;
    }

    std::string to_utf8(const std::wstring& value)
    {
        return value.empty() ? std::string{} : to_utf8(value.c_str(), "");
    }

    std::wstring read_env_var(const wchar_t* name)
    {
        wchar_t* buffer = nullptr;
        size_t length = 0;
        const errno_t result = _wdupenv_s(&buffer, &length, name);
        if (result != 0 || buffer == nullptr)
        {
            return {};
        }

        std::wstring value(buffer, length > 0 ? length - 1 : 0);
        free(buffer);
        return value;
    }

    std::string read_host()
    {
        const auto value = read_env_var(L"EF_OVERLAY_HOST");
        return value.empty() ? std::string{"127.0.0.1"} : to_utf8(value.c_str(), "127.0.0.1");
    }

    int read_port()
    {
        const auto value = read_env_var(L"EF_OVERLAY_PORT");
        if (value.empty())
        {
            return default_port;
        }

        const int candidate = _wtoi(value.c_str());
        if (candidate <= 0 || candidate > 65535)
        {
            spdlog::warn("EF_OVERLAY_PORT value '{}' is out of range; using default {}", to_utf8(value), default_port);
            return default_port;
        }

        return candidate;
    }

    std::string read_token()
    {
        const auto value = read_env_var(L"EF_OVERLAY_TOKEN");
        return value.empty() ? std::string{} : to_utf8(value);
    }

    std::wstring get_executable_path()
    {
        std::wstring buffer(MAX_PATH, L'\0');
        DWORD copied = ::GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        while (copied >= buffer.size())
        {
            buffer.resize(buffer.size() * 2);
            copied = ::GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        }

        buffer.resize(copied);
        return buffer;
    }

    ProgramOptions parse_options(int argc, wchar_t** argv)
    {
        ProgramOptions options;
        for (int i = 1; i < argc; ++i)
        {
            const std::wstring arg = argv[i];
            if (arg == L"--register-protocol")
            {
                options.mode = Mode::RegisterProtocol;
                break;
            }
            if (arg == L"--unregister-protocol")
            {
                options.mode = Mode::UnregisterProtocol;
                break;
            }
            if (arg.rfind(protocol_scheme, 0) == 0)
            {
                options.mode = Mode::HandleUri;
                options.uri = arg;
                break;
            }
            if (arg == L"--handle-uri" && i + 1 < argc)
            {
                options.mode = Mode::HandleUri;
                options.uri = argv[++i];
                break;
            }
        }

        return options;
    }

    int hex_value(char c)
    {
        if (c >= '0' && c <= '9')
        {
            return c - '0';
        }
        if (c >= 'a' && c <= 'f')
        {
            return c - 'a' + 10;
        }
        if (c >= 'A' && c <= 'F')
        {
            return c - 'A' + 10;
        }
        return -1;
    }

    std::string url_decode(const std::string& input)
    {
        std::string output;
        output.reserve(input.size());

        for (std::size_t i = 0; i < input.size(); ++i)
        {
            const char c = input[i];
            if (c == '%' && i + 2 < input.size())
            {
                const int hi = hex_value(input[i + 1]);
                const int lo = hex_value(input[i + 2]);
                if (hi >= 0 && lo >= 0)
                {
                    output.push_back(static_cast<char>((hi << 4) | lo));
                    i += 2;
                    continue;
                }
            }
            else if (c == '+')
            {
                output.push_back(' ');
                continue;
            }

            output.push_back(c);
        }

        return output;
    }

    std::unordered_map<std::string, std::string> parse_query(const std::string& query)
    {
        std::unordered_map<std::string, std::string> params;
        std::size_t pos = 0;
        while (pos < query.size())
        {
            const auto amp = query.find('&', pos);
            const auto pair = query.substr(pos, amp == std::string::npos ? std::string::npos : amp - pos);
            const auto eq = pair.find('=');
            if (eq != std::string::npos)
            {
                const auto key = url_decode(pair.substr(0, eq));
                const auto value = url_decode(pair.substr(eq + 1));
                params[key] = value;
            }
            pos = (amp == std::string::npos) ? query.size() : amp + 1;
        }

        return params;
    }

    std::optional<UriCommand> parse_uri_command(const std::wstring& uri, std::string& error)
    {
        const std::string utf8 = to_utf8(uri);
        if (utf8.rfind("ef-overlay://", 0) != 0)
        {
            error = "URI does not use ef-overlay scheme";
            return std::nullopt;
        }

        const auto without_scheme = utf8.substr(std::char_traits<char>::length("ef-overlay://"));
        const auto question = without_scheme.find('?');
        std::string path = without_scheme.substr(0, question);
        if (!path.empty() && path.front() == '/')
        {
            path.erase(path.begin());
        }

        const std::string query = question == std::string::npos ? std::string{} : without_scheme.substr(question + 1);
        auto params = parse_query(query);

        auto tokenIt = params.find("token");
        if (tokenIt == params.end())
        {
            error = "Missing required token parameter";
            return std::nullopt;
        }

        UriCommand command;
        command.action = path;
        command.token = tokenIt->second;
        params.erase(tokenIt);

        if (command.action == "overlay-state")
        {
            auto payloadIt = params.find("payload");
            if (payloadIt == params.end())
            {
                error = "overlay-state command requires payload parameter";
                return std::nullopt;
            }

            const std::string decoded = payloadIt->second;
            auto json = nlohmann::json::parse(decoded, nullptr, false);
            if (json.is_discarded())
            {
                error = "Invalid JSON payload";
                return std::nullopt;
            }

            command.payload = std::move(json);
            command.payloadSerialized = command.payload->dump();
            command.payloadBytes = decoded.size();
        }
        else if (command.action == "ping")
        {
            // no-op
        }
        else
        {
            error = "Unsupported command: " + command.action;
            return std::nullopt;
        }

        return command;
    }

    bool forward_command_http(const UriCommand& command, const std::string& host, int port, const std::string& token)
    {
        httplib::Client client(host, port);
        client.set_connection_timeout(1, 0);
        client.set_read_timeout(2, 0);

        httplib::Headers headers;
        if (!token.empty())
        {
            headers.emplace("x-ef-overlay-token", token);
        }

        if (command.action == "overlay-state" && command.payload)
        {
            auto res = client.Post("/overlay/state", headers, command.payloadSerialized, "application/json");
            return res && res->status >= 200 && res->status < 300;
        }

        if (command.action == "ping")
        {
            auto res = client.Get("/health");
            return res && res->status == 200;
        }

        return false;
    }

    void apply_command(HelperRuntime& runtime, const UriCommand& command)
    {
        auto& server = runtime.server();
        if (command.action == "overlay-state" && command.payload)
        {
            try
            {
                const auto state = overlay::parse_overlay_state(*command.payload);
                server.ingestOverlayState(state, command.payloadBytes, "protocol");
                spdlog::info("overlay-state command applied via protocol link");
            }
            catch (const std::exception& ex)
            {
                spdlog::error("Rejected overlay-state command due to validation error: {}", ex.what());
            }
        }
        else if (command.action == "ping")
        {
            spdlog::info("Received protocol ping command");
        }
    }

    void configure_logging()
    {
        if (!spdlog::default_logger())
        {
            auto logger = spdlog::stdout_color_mt("ef-overlay-helper");
            spdlog::set_default_logger(logger);
        }

        spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] %v");
        spdlog::info("{} starting up", helper_name);
    }
}

int wmain(int argc, wchar_t* argv[])
{
    ProgramOptions options = parse_options(argc, argv);
    configure_logging();

    if (options.mode == Mode::RegisterProtocol || options.mode == Mode::UnregisterProtocol)
    {
        const auto exePath = get_executable_path();
        if (exePath.empty())
        {
            spdlog::error("Unable to determine executable path for protocol registration");
            spdlog::shutdown();
            return 1;
        }

        const bool ok = options.mode == Mode::RegisterProtocol
            ? register_overlay_protocol(exePath)
            : unregister_overlay_protocol();
        spdlog::shutdown();
        return ok ? 0 : 1;
    }

    if (!::SetConsoleCtrlHandler(console_ctrl_handler, TRUE))
    {
        spdlog::warn("Failed to install console control handler");
    }

    const auto host = read_host();
    const auto port = read_port();
    const auto token = read_token();

    if (token.empty())
    {
        spdlog::warn("EF_OVERLAY_TOKEN is not set; HTTP and protocol commands will be accepted without authentication");
    }

    std::optional<UriCommand> deferredCommand;
    if (options.mode == Mode::HandleUri)
    {
        std::string error;
        auto parsed = parse_uri_command(options.uri, error);
        if (!parsed)
        {
            spdlog::error("Failed to parse ef-overlay URI: {}", error);
            spdlog::shutdown();
            return 1;
        }

        if (!token.empty() && parsed->token != token)
        {
            spdlog::error("Rejected ef-overlay command due to token mismatch");
            spdlog::shutdown();
            return 1;
        }

        if (forward_command_http(*parsed, host, port, token))
        {
            spdlog::info("Forwarded '{}' command to existing helper instance", parsed->action);
            spdlog::shutdown();
            return 0;
        }

        spdlog::info("No existing helper responded on {}:{}; starting local instance", host, port);
        deferredCommand = std::move(parsed.value());
    }

    HelperRuntime::Config runtimeConfig;
    runtimeConfig.host = host;
    runtimeConfig.port = port;
    runtimeConfig.token = token;
    runtimeConfig.executableDirectory = std::filesystem::path(get_executable_path()).parent_path();

    HelperRuntime runtime(std::move(runtimeConfig));
    if (!runtime.start())
    {
        spdlog::error("Unable to start helper server on {}:{}", host, port);
        spdlog::shutdown();
        return 1;
    }

    const auto& server = runtime.server();

    spdlog::info("Helper HTTP API available at http://{}:{}{}", host, port, server.requiresAuth() ? " (auth required)" : "");

    if (deferredCommand)
    {
        apply_command(runtime, *deferredCommand);
    }

    spdlog::info("Press Ctrl+C to shut down.");
    while (is_running())
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    spdlog::info("Shutdown signal received; stopping server...");
    runtime.stop();

    spdlog::info("Helper terminated cleanly.");
    spdlog::shutdown();
    return 0;
}
