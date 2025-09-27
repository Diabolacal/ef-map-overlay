#include "protocol_registration.hpp"

#include <windows.h>

#include <spdlog/spdlog.h>

namespace
{
    constexpr const wchar_t* kProtocolKey = L"Software\\Classes\\ef-overlay";

    std::string wide_to_utf8(const std::wstring& value)
    {
        if (value.empty())
        {
            return {};
        }

        const int size = ::WideCharToMultiByte(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
        if (size <= 0)
        {
            return {};
        }

        std::string buffer(static_cast<std::size_t>(size), '\0');
        ::WideCharToMultiByte(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), buffer.data(), size, nullptr, nullptr);
        return buffer;
    }

    bool set_string_value(HKEY key, const wchar_t* name, const std::wstring& value)
    {
        const auto byteSize = static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t));
        const LONG status = ::RegSetValueExW(key, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(value.c_str()), byteSize);
        if (status != ERROR_SUCCESS)
        {
            const std::wstring label = name ? std::wstring{name} : std::wstring{L"(default)"};
            spdlog::error("Failed to set registry value '{}' (error {})", wide_to_utf8(label), status);
            return false;
        }
        return true;
    }
}

bool register_overlay_protocol(const std::wstring& executablePath)
{
    HKEY rootKey{};
    DWORD disposition = 0;
    LONG status = ::RegCreateKeyExW(HKEY_CURRENT_USER, kProtocolKey, 0, nullptr, 0, KEY_WRITE, nullptr, &rootKey, &disposition);
    if (status != ERROR_SUCCESS)
    {
        spdlog::error("Failed to open protocol registry key (error {})", status);
        return false;
    }

    bool success = true;
    success &= set_string_value(rootKey, nullptr, L"URL:EF Overlay");
    success &= set_string_value(rootKey, L"URL Protocol", L"");

    if (success)
    {
        const std::wstring iconKeyPath = std::wstring(kProtocolKey) + L"\\DefaultIcon";
        HKEY iconKey{};
        status = ::RegCreateKeyExW(HKEY_CURRENT_USER, iconKeyPath.c_str(), 0, nullptr, 0, KEY_WRITE, nullptr, &iconKey, nullptr);
        if (status == ERROR_SUCCESS)
        {
            success &= set_string_value(iconKey, nullptr, executablePath + L",0");
            ::RegCloseKey(iconKey);
        }
        else
        {
            spdlog::warn("Failed to open DefaultIcon key (error {})", status);
        }
    }

    if (success)
    {
        const std::wstring commandKeyPath = std::wstring(kProtocolKey) + L"\\shell\\open\\command";
        HKEY commandKey{};
        status = ::RegCreateKeyExW(HKEY_CURRENT_USER, commandKeyPath.c_str(), 0, nullptr, 0, KEY_WRITE, nullptr, &commandKey, nullptr);
        if (status == ERROR_SUCCESS)
        {
            const std::wstring command = L"\"" + executablePath + L"\" \"%1\"";
            success &= set_string_value(commandKey, nullptr, command);
            ::RegCloseKey(commandKey);
        }
        else
        {
            spdlog::error("Failed to open command key (error {})", status);
            success = false;
        }
    }

    ::RegCloseKey(rootKey);

    if (success)
    {
        spdlog::info("ef-overlay:// protocol registered for {}", wide_to_utf8(executablePath));
    }

    return success;
}

bool unregister_overlay_protocol()
{
    const LONG status = ::RegDeleteTreeW(HKEY_CURRENT_USER, kProtocolKey);
    if (status != ERROR_SUCCESS && status != ERROR_FILE_NOT_FOUND)
    {
        spdlog::error("Failed to remove protocol registration (error {})", status);
        return false;
    }

    spdlog::info("ef-overlay:// protocol unregistered.");
    return true;
}
