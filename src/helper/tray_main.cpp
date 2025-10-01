#include "tray_application.hpp"

#include <objbase.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include <cstdlib>
#include <filesystem>
#include <string>

namespace
{
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

    std::string to_utf8(const std::wstring& value)
    {
        if (value.empty())
        {
            return {};
        }

        const int size = ::WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, nullptr, 0, nullptr, nullptr);
        if (size <= 1)
        {
            return {};
        }

        std::string buffer(static_cast<std::size_t>(size - 1), '\0');
        ::WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, buffer.data(), size, nullptr, nullptr);
        return buffer;
    }

    std::string read_host()
    {
        const auto value = read_env_var(L"EF_OVERLAY_HOST");
        return value.empty() ? std::string{"127.0.0.1"} : to_utf8(value);
    }

    int read_port()
    {
        const auto value = read_env_var(L"EF_OVERLAY_PORT");
        if (value.empty())
        {
            return 38765;
        }

        const int candidate = _wtoi(value.c_str());
        if (candidate <= 0 || candidate > 65535)
        {
            spdlog::warn("EF_OVERLAY_PORT value '{}' is out of range; using default 38765", to_utf8(value));
            return 38765;
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

    void configure_logging()
    {
        if (!spdlog::default_logger())
        {
            auto logger = spdlog::stdout_color_mt("ef-overlay-helper-tray");
            spdlog::set_default_logger(logger);
        }

        spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] %v");
        spdlog::info("ef-overlay-tray starting up");
    }
}

int APIENTRY wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR, int)
{
    ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    configure_logging();

    HelperRuntime::Config config;
    config.host = read_host();
    config.port = read_port();
    config.token = read_token();
    config.executableDirectory = std::filesystem::path(get_executable_path()).parent_path();

    HelperRuntime runtime(std::move(config));
    HelperTrayApplication app(instance, runtime);
    const int result = app.run();

    runtime.stop();
    spdlog::shutdown();
    ::CoUninitialize();
    return result;
}
