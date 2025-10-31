#include "tray_application.hpp"

#include <shellapi.h>
#include <shlobj.h>
#include <tlhelp32.h>
#include <spdlog/spdlog.h>

#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <vector>

namespace
{
    constexpr wchar_t kWindowClassName[] = L"EFOverlayTrayWindow";
        constexpr wchar_t kBulletChar = 0x2022;

    std::wstring utf8_to_wide(const std::string& value)
    {
        if (value.empty())
        {
            return {};
        }

        const int size = ::MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, nullptr, 0);
        if (size <= 1)
        {
            return {};
        }

        std::wstring buffer(static_cast<std::size_t>(size - 1), L'\0');
        ::MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, buffer.data(), size);
        return buffer;
    }

    std::wstring truncate_tooltip(const std::wstring& value, std::size_t maxLength)
    {
        if (value.size() <= maxLength)
        {
            return value;
        }

        if (maxLength < 3)
        {
            return value.substr(0, maxLength);
        }

        return value.substr(0, maxLength - 3) + L"...";
    }

    std::filesystem::path resolve_log_directory()
    {
        PWSTR rawPath = nullptr;
        std::filesystem::path result;
        if (SUCCEEDED(::SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_CREATE, nullptr, &rawPath)))
        {
            result = std::filesystem::path(rawPath) / L"EFOverlay" / L"logs";
            ::CoTaskMemFree(rawPath);
        }
        else
        {
            result = std::filesystem::temp_directory_path() / L"EFOverlay" / L"logs";
        }

        std::error_code ec;
        std::filesystem::create_directories(result, ec);
        return result;
    }

    HICON load_tray_icon()
    {
        // Try to load icon from MSIX package Assets folder
        wchar_t exePath[MAX_PATH];
        if (::GetModuleFileNameW(nullptr, exePath, MAX_PATH) > 0)
        {
            std::filesystem::path exeDir = std::filesystem::path(exePath).parent_path();
            std::filesystem::path iconPath = exeDir / L"Assets" / L"Square44x44Logo.png";
            
            // Try loading as .ico file first (if we have one)
            std::filesystem::path icoPath = exeDir / L"Assets" / L"app.ico";
            if (std::filesystem::exists(icoPath))
            {
                HICON icon = static_cast<HICON>(::LoadImageW(
                    nullptr,
                    icoPath.wstring().c_str(),
                    IMAGE_ICON,
                    16, 16,  // Small icon size for tray
                    LR_LOADFROMFILE | LR_DEFAULTCOLOR
                ));
                if (icon)
                {
                    return icon;
                }
            }
            
            // Fallback: try loading PNG as icon (Windows 10+ supports this)
            if (std::filesystem::exists(iconPath))
            {
                HICON icon = static_cast<HICON>(::LoadImageW(
                    nullptr,
                    iconPath.wstring().c_str(),
                    IMAGE_ICON,
                    16, 16,
                    LR_LOADFROMFILE | LR_DEFAULTCOLOR
                ));
                if (icon)
                {
                    return icon;
                }
            }
        }
        
        // Fallback to default Windows application icon
        return ::LoadIconW(nullptr, IDI_APPLICATION);
    }

    std::wstring format_double(double value)
    {
        std::wostringstream oss;
        const double magnitude = std::fabs(value);
        int precision = 2;
        if (magnitude >= 1000.0)
        {
            precision = 0;
        }
        else if (magnitude >= 100.0)
        {
            precision = 1;
        }
        oss << std::fixed << std::setprecision(precision) << value;
        return oss.str();
    }

    std::wstring build_telemetry_url(const HelperRuntime& runtime, const wchar_t* path)
    {
        std::wstring host = utf8_to_wide(runtime.server().host());
        if (host.empty())
        {
            host = L"127.0.0.1";
        }

        std::wostringstream oss;
        oss << L"http://" << host << L":" << runtime.server().port() << path;
        return oss.str();
    }

    bool open_url(const std::wstring& url)
    {
        return reinterpret_cast<INT_PTR>(::ShellExecuteW(nullptr, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL)) > 32;
    }
}

HelperTrayApplication::HelperTrayApplication(HINSTANCE instance, HelperRuntime& runtime)
    : hInstance_(instance)
    , runtime_(runtime)
{
    // Load config to restore debug logging preference
    loadConfig();
    
    // Apply debug logging level if enabled
    if (debugLoggingEnabled_)
    {
        spdlog::set_level(spdlog::level::debug);
        spdlog::debug("Debug logging enabled from config");
    }
}

HelperTrayApplication::~HelperTrayApplication()
{
    removeTrayIcon();
}

int HelperTrayApplication::run()
{
    if (!registerWindowClass())
    {
        return 1;
    }

    if (!createWindow())
    {
        return 1;
    }

    addTrayIcon();

    if (!runtime_.start())
    {
        postBalloon(L"EF Overlay Helper", L"Failed to start helper runtime", NIIF_ERROR);
    }

    updateTooltip();
    statusTimerId_ = ::SetTimer(hwnd_, kStatusTimerId, 1500, nullptr);

    MSG msg;
    while (::GetMessageW(&msg, nullptr, 0, 0) > 0)
    {
        ::TranslateMessage(&msg);
        ::DispatchMessageW(&msg);
    }

    if (statusTimerId_ != 0)
    {
        ::KillTimer(hwnd_, statusTimerId_);
        statusTimerId_ = 0;
    }

    return static_cast<int>(msg.wParam);
}

bool HelperTrayApplication::registerWindowClass()
{
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = &HelperTrayApplication::WindowProc;
    wc.hInstance = hInstance_;
    wc.lpszClassName = kWindowClassName;
    wc.hCursor = ::LoadCursor(nullptr, IDC_ARROW);
    wc.hIcon = ::LoadIcon(nullptr, IDI_APPLICATION);

    return ::RegisterClassExW(&wc) != 0;
}

bool HelperTrayApplication::createWindow()
{
    hwnd_ = ::CreateWindowExW(
        0,
        kWindowClassName,
        L"EF Overlay Tray",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        nullptr,
        nullptr,
        hInstance_,
        this);

    if (!hwnd_)
    {
        return false;
    }

    return true;
}

void HelperTrayApplication::addTrayIcon()
{
    iconData_ = {};
    iconData_.cbSize = sizeof(iconData_);
    iconData_.hWnd = hwnd_;
    iconData_.uID = 1;
    iconData_.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    iconData_.uCallbackMessage = kTrayMessage;
    iconData_.hIcon = load_tray_icon();
    ::wcscpy_s(iconData_.szTip, L"EF Overlay Helper");

    iconAdded_ = ::Shell_NotifyIconW(NIM_ADD, &iconData_) != FALSE;
    if (!iconAdded_)
    {
        spdlog::error("Failed to add helper tray icon");
    }
}

void HelperTrayApplication::removeTrayIcon()
{
    if (iconAdded_)
    {
        ::Shell_NotifyIconW(NIM_DELETE, &iconData_);
        iconAdded_ = false;
    }
}

void HelperTrayApplication::refreshTrayIcon()
{
    if (iconAdded_)
    {
        ::Shell_NotifyIconW(NIM_MODIFY, &iconData_);
    }
}

void HelperTrayApplication::updateTooltip()
{
    if (!iconAdded_)
    {
        return;
    }

    const auto tooltip = truncate_tooltip(buildTooltip(), sizeof(iconData_.szTip) / sizeof(wchar_t) - 1);
    ::wcscpy_s(iconData_.szTip, tooltip.c_str());
    refreshTrayIcon();
}

void HelperTrayApplication::showContextMenu()
{
    HMENU menu = ::CreatePopupMenu();
    if (!menu)
    {
        return;
    }

    const bool running = runtime_.isRunning();

    constexpr UINT appendPos = static_cast<UINT>(-1);
    ::InsertMenuW(menu, appendPos, MF_BYPOSITION | (running ? MF_GRAYED : MF_ENABLED), static_cast<UINT>(MenuId::Start), L"Start helper");
    ::InsertMenuW(menu, appendPos, MF_BYPOSITION | (running ? MF_ENABLED : MF_GRAYED), static_cast<UINT>(MenuId::Stop), L"Stop helper");
    ::InsertMenuW(menu, appendPos, MF_BYPOSITION | MF_SEPARATOR, 0, nullptr);
    ::InsertMenuW(menu, appendPos, MF_BYPOSITION | MF_ENABLED, static_cast<UINT>(MenuId::SampleState), L"Post sample overlay state");
    ::InsertMenuW(menu, appendPos, MF_BYPOSITION | MF_ENABLED, static_cast<UINT>(MenuId::Inject), L"Start Overlay");
    ::InsertMenuW(menu, appendPos, MF_BYPOSITION | MF_SEPARATOR, 0, nullptr);
    ::InsertMenuW(menu, appendPos, MF_BYPOSITION | (debugLoggingEnabled_ ? MF_CHECKED : MF_UNCHECKED), static_cast<UINT>(MenuId::ToggleDebugLogging), L"Enable debug logging");
    ::InsertMenuW(menu, appendPos, MF_BYPOSITION | MF_ENABLED, static_cast<UINT>(MenuId::ExportDebugLogs), L"Export debug logs...");
    ::InsertMenuW(menu, appendPos, MF_BYPOSITION | MF_ENABLED, static_cast<UINT>(MenuId::OpenHelperLogs), L"Open helper logs folder");
    ::InsertMenuW(menu, appendPos, MF_BYPOSITION | MF_ENABLED, static_cast<UINT>(MenuId::OpenGameLogs), L"Open game logs folder");
    ::InsertMenuW(menu, appendPos, MF_BYPOSITION | MF_SEPARATOR, 0, nullptr);
    ::InsertMenuW(menu, appendPos, MF_BYPOSITION | MF_ENABLED, static_cast<UINT>(MenuId::CopyDiagnostics), L"Copy diagnostics to clipboard");
    ::InsertMenuW(menu, appendPos, MF_BYPOSITION | (running ? MF_ENABLED : MF_GRAYED), static_cast<UINT>(MenuId::OpenTelemetryHistory), L"Open telemetry history");
    ::InsertMenuW(menu, appendPos, MF_BYPOSITION | (running ? MF_ENABLED : MF_GRAYED), static_cast<UINT>(MenuId::ResetTelemetry), L"Reset telemetry session");
    ::InsertMenuW(menu, appendPos, MF_BYPOSITION | MF_SEPARATOR, 0, nullptr);
    ::InsertMenuW(menu, appendPos, MF_BYPOSITION | MF_ENABLED, static_cast<UINT>(MenuId::Exit), L"Exit");

    ::SetForegroundWindow(hwnd_);

    POINT cursor{};
    ::GetCursorPos(&cursor);
    ::TrackPopupMenu(menu, TPM_LEFTALIGN | TPM_BOTTOMALIGN | TPM_RIGHTBUTTON, cursor.x, cursor.y, 0, hwnd_, nullptr);

    ::DestroyMenu(menu);
}

void HelperTrayApplication::handleCommand(MenuId id)
{
    switch (id)
    {
        case MenuId::Start:
        {
            if (runtime_.start())
            {
                postBalloon(L"EF Overlay Helper", L"Helper runtime started", NIIF_INFO);
            }
            else
            {
                postBalloon(L"EF Overlay Helper", L"Failed to start helper runtime", NIIF_ERROR);
            }
            updateTooltip();
            break;
        }
        case MenuId::Stop:
        {
            runtime_.stop();
            postBalloon(L"EF Overlay Helper", L"Helper runtime stopped", NIIF_INFO);
            updateTooltip();
            break;
        }
        case MenuId::SampleState:
        {
            if (runtime_.postSampleOverlayState())
            {
                postBalloon(L"Overlay sample", L"Sample route posted to overlay", NIIF_INFO);
            }
            else
            {
                const auto status = runtime_.getStatus();
                auto message = utf8_to_wide(status.lastErrorMessage);
                if (message.empty())
                {
                    message = L"Unable to post sample overlay state";
                }
                postBalloon(L"Overlay sample", message, NIIF_ERROR);
            }
            updateTooltip();
            break;
        }
        case MenuId::Inject:
        {
            if (runtime_.injectOverlay())
            {
                postBalloon(L"Overlay injector", L"Overlay DLL injected successfully", NIIF_INFO);
            }
            else
            {
                const auto status = runtime_.getStatus();
                auto message = utf8_to_wide(status.lastInjectionMessage);
                if (message.empty())
                {
                    message = L"Injector reported an error";
                }
                postBalloon(L"Overlay injector", message, NIIF_ERROR);
            }
            updateTooltip();
            break;
        }
        case MenuId::OpenHelperLogs:
        {
            const std::filesystem::path target = resolve_log_directory();
            const auto logString = target.wstring();
            if (reinterpret_cast<INT_PTR>(::ShellExecuteW(nullptr, L"open", logString.c_str(), nullptr, nullptr, SW_SHOWNORMAL)) <= 32)
            {
                std::wstring message = L"Unable to open helper logs directory: " + logString;
                postBalloon(L"Helper Logs", message, NIIF_ERROR);
            }
            else
            {
                postBalloon(L"Helper Logs", L"Opened helper logs directory", NIIF_INFO);
            }
            break;
        }
        case MenuId::OpenGameLogs:
        {
            const auto status = runtime_.getStatus();
            std::filesystem::path target;
            if (!status.chatLogFile.empty())
            {
                target = status.chatLogFile.parent_path();
            }
            else if (!status.chatLogDirectory.empty())
            {
                target = status.chatLogDirectory;
            }
            else
            {
                // Game logs not available
                postBalloon(L"Game Logs", L"Game log directory not available. Make sure the game is running and logs are enabled.", NIIF_WARNING);
                break;
            }

            const auto logString = target.wstring();
            if (reinterpret_cast<INT_PTR>(::ShellExecuteW(nullptr, L"open", logString.c_str(), nullptr, nullptr, SW_SHOWNORMAL)) <= 32)
            {
                std::wstring message = L"Unable to open game logs directory: " + logString;
                postBalloon(L"Game Logs", message, NIIF_ERROR);
            }
            else
            {
                postBalloon(L"Game Logs", L"Opened game logs directory", NIIF_INFO);
            }
            break;
        }
        case MenuId::CopyDiagnostics:
        {
            const auto diagnostics = buildDiagnosticsText();
            if (!diagnostics.empty())
            {
                copyDiagnosticsToClipboard(diagnostics);
                postBalloon(L"Diagnostics", L"Status copied to clipboard", NIIF_INFO);
            }
            else
            {
                postBalloon(L"Diagnostics", L"No diagnostics available", NIIF_WARNING);
            }
            break;
        }
        case MenuId::OpenTelemetryHistory:
        {
            const auto url = build_telemetry_url(runtime_, L"/telemetry/history");
            if (open_url(url))
            {
                postBalloon(L"Telemetry history", L"Opened telemetry history in browser", NIIF_INFO);
            }
            else
            {
                std::wstring message = L"Unable to open " + truncate_tooltip(url, 60);
                postBalloon(L"Telemetry history", message, NIIF_ERROR);
            }
            break;
        }
        case MenuId::ResetTelemetry:
        {
            auto summary = runtime_.resetTelemetrySession();
            if (summary.has_value())
            {
                std::wstring message = L"Telemetry history reset";
                if (summary->history.has_value())
                {
                    message += L" (" + std::to_wstring(summary->history->slices.size()) + L" slices)";
                }
            postBalloon(L"Telemetry reset", message, NIIF_INFO);
            }
            else
            {
                const auto status = runtime_.getStatus();
                auto message = utf8_to_wide(status.lastErrorMessage);
                if (message.empty())
                {
                    message = L"Telemetry reset failed";
                }
                postBalloon(L"Telemetry reset", message, NIIF_ERROR);
            }
            updateTooltip();
            break;
        }
        case MenuId::ToggleDebugLogging:
        {
            toggleDebugLogging();
            break;
        }
        case MenuId::ExportDebugLogs:
        {
            exportDebugLogs();
            break;
        }
        case MenuId::Exit:
        {
            ::DestroyWindow(hwnd_);
            break;
        }
    }
}

void HelperTrayApplication::handleTrayEvent(LPARAM lParam)
{
    switch (static_cast<UINT>(lParam))
    {
        case WM_RBUTTONUP:
        case WM_CONTEXTMENU:
            showContextMenu();
            break;
        case WM_LBUTTONDBLCLK:
            if (!runtime_.isRunning())
            {
                runtime_.start();
            }
            else
            {
                runtime_.postSampleOverlayState();
            }
            updateTooltip();
            break;
        default:
            break;
    }
}

void HelperTrayApplication::handleTimer(UINT_PTR id)
{
    if (id == kStatusTimerId)
    {
        updateTooltip();
    }
}

void HelperTrayApplication::postBalloon(const std::wstring& title, const std::wstring& message, DWORD flags) const
{
    if (!iconAdded_)
    {
        return;
    }

    NOTIFYICONDATAW data = iconData_;
    data.uFlags = NIF_INFO;
    ::wcscpy_s(data.szInfoTitle, truncate_tooltip(title, 63).c_str());
    ::wcscpy_s(data.szInfo, truncate_tooltip(message, 255).c_str());
    data.dwInfoFlags = flags;
    ::Shell_NotifyIconW(NIM_MODIFY, &data);
}

std::wstring HelperTrayApplication::buildTooltip() const
{
    const auto status = runtime_.getStatus();
    std::wostringstream tip;
    tip << L"EF Overlay Helper" << L"\n";
    tip << (status.serverRunning ? L"Running" : L"Stopped");

    if (status.hasOverlayState)
        {
            tip << L' ' << kBulletChar << L" payload";
    }

    if (status.eventsRecorded > 0)
    {
            tip << L' ' << kBulletChar << L" events:" << status.eventsRecorded;
        if (status.eventsDropped > 0)
        {
            tip << L" (" << status.eventsDropped << L" dropped)";
        }
    }

    const auto overlayLine = formatOverlayLine(status);
    if (!overlayLine.empty())
    {
        tip << L"\n" << overlayLine;
    }

    if (status.lastSamplePostedAt)
    {
        tip << L"\nSample: " << formatRelativeTime(status.lastSamplePostedAt);
    }

    if (status.lastInjectionAt)
    {
        tip << L"\nInject: " << formatRelativeTime(status.lastInjectionAt);
    }

    const auto logLine = formatLogWatcherLine(status);
    if (!logLine.empty())
    {
        tip << L"\n" << logLine;
    }

    const auto combatLine = formatCombatLine(status);
    if (!combatLine.empty())
    {
        tip << L"\n" << combatLine;
    }

    const auto telemetryLine = formatTelemetryLine(status);
    if (!telemetryLine.empty())
    {
        tip << L"\n" << telemetryLine;
    }

    if (!status.lastErrorMessage.empty())
    {
        tip << L"\nErr: " << truncate_tooltip(utf8_to_wide(status.lastErrorMessage), 40);
    }

    if (!status.logWatcherError.empty())
    {
        tip << L"\nWatcher: " << truncate_tooltip(utf8_to_wide(status.logWatcherError), 40);
    }

    return tip.str();
}

std::wstring HelperTrayApplication::buildDiagnosticsText() const
{
    const auto status = runtime_.getStatus();
    std::wostringstream out;
    out << L"EF Overlay Helper diagnostics" << L"\r\n";
    out << L"Server: " << (status.serverRunning ? L"running" : L"stopped")
        << L" on " << utf8_to_wide(runtime_.server().host()) << L":" << runtime_.server().port() << L"\r\n";
    out << L"Overlay state: " << (status.hasOverlayState ? L"available" : L"none");
    if (status.lastOverlayGeneratedAt)
    {
        out << L" | generated " << formatRelativeTime(status.lastOverlayGeneratedAt);
    }
    if (status.lastOverlayAcceptedAt)
    {
        out << L" | ingested " << formatRelativeTime(status.lastOverlayAcceptedAt);
    }
    out << L"\r\n";
    out << L"Events: recorded=" << status.eventsRecorded << L" buffered=" << status.eventsBuffered << L" dropped=" << status.eventsDropped << L"\r\n";

    out << L"Log watcher: " << (status.logWatcherRunning ? L"running" : L"stopped");
    if (!status.chatLogDirectory.empty())
    {
        out << L" | chat=" << status.chatLogDirectory.wstring();
        if (!status.chatLogFile.empty())
        {
            out << L" (" << status.chatLogFile.filename().wstring() << L")";
        }
    }
    if (!status.combatLogDirectory.empty())
    {
        out << L" | combat=" << status.combatLogDirectory.wstring();
        if (!status.combatLogFile.empty())
        {
            out << L" (" << status.combatLogFile.filename().wstring() << L")";
        }
    }
    if (status.location)
    {
        out << L"\r\nLocation: " << utf8_to_wide(status.location->systemName);
        if (status.location->observedAt.time_since_epoch().count() != 0)
        {
            out << L" @ " << formatRelativeTime(std::optional<std::chrono::system_clock::time_point>{status.location->observedAt});
        }
    }
    else
    {
        out << L"\r\nLocation: pending";
    }
    if (!status.logWatcherError.empty())
    {
        out << L"\r\nWatcher error: " << utf8_to_wide(status.logWatcherError);
    }

    if (status.combat)
    {
        out << L"\r\nCombat: events=" << status.combat->combatEventCount
            << L" notify=" << status.combat->notifyEventCount;
        if (!status.combat->characterId.empty())
        {
            out << L" | character=" << utf8_to_wide(status.combat->characterId);
        }
        if (!status.combat->lastCombatLine.empty())
        {
            out << L"\r\nLast combat line: " << utf8_to_wide(status.combat->lastCombatLine);
        }
    }

    const auto telemetryLine = formatTelemetryLine(status);
    if (!telemetryLine.empty())
    {
        out << L"\r\n" << telemetryLine;
    }

    if (status.telemetry.mining.has_value() && status.telemetry.mining->hasData() && !status.telemetry.mining->buckets.empty())
    {
        out << L"\r\nTelemetry buckets:";
        std::size_t count = 0;
        for (const auto& bucket : status.telemetry.mining->buckets)
        {
            if (count++ >= 3)
            {
                out << L" ...";
                break;
            }
            out << L" " << utf8_to_wide(bucket.resource) << L"=" << format_double(bucket.sessionTotalM3);
        }
    }

    if (status.telemetry.history.has_value())
    {
        out << L"\r\nTelemetry history: slices=" << status.telemetry.history->slices.size()
            << L"/" << status.telemetry.history->capacity
            << L" (" << format_double(status.telemetry.history->sliceSeconds) << L"s)";
        if (!status.telemetry.history->resetMarkersMs.empty())
        {
            out << L" | resets=" << status.telemetry.history->resetMarkersMs.size();
        }
    }

    if (status.lastTelemetryResetAt)
    {
        out << L"\r\nTelemetry last reset: " << formatRelativeTime(status.lastTelemetryResetAt);
    }

    if (!status.lastErrorMessage.empty())
    {
        out << L"\r\nLast error: " << utf8_to_wide(status.lastErrorMessage);
    }

    return out.str();
}

void HelperTrayApplication::copyDiagnosticsToClipboard(const std::wstring& text) const
{
    if (text.empty())
    {
        return;
    }

    if (!::OpenClipboard(hwnd_))
    {
        return;
    }

    const SIZE_T bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL handle = ::GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (!handle)
    {
        ::CloseClipboard();
        return;
    }

    void* data = ::GlobalLock(handle);
    if (!data)
    {
        ::GlobalFree(handle);
        ::CloseClipboard();
        return;
    }

    std::memcpy(data, text.c_str(), bytes);
    ::GlobalUnlock(handle);

    ::EmptyClipboard();
    ::SetClipboardData(CF_UNICODETEXT, handle);
    ::CloseClipboard();
}

std::wstring HelperTrayApplication::formatPath(const std::filesystem::path& path, std::size_t maxLength) const
{
    if (path.empty())
    {
        return {};
    }

    auto text = path.filename().wstring();
    if (text.empty())
    {
        text = path.wstring();
    }
    return truncate_tooltip(text, maxLength);
}

std::wstring HelperTrayApplication::formatLogWatcherLine(const HelperRuntime::Status& status) const
{
    if (!status.logWatcherRunning && status.logWatcherError.empty())
    {
        return {};
    }

    std::wostringstream oss;
    oss << L"Log watcher: " << (status.logWatcherRunning ? L"active" : L"stopped");
    if (status.location)
    {
    oss << L' ' << kBulletChar << L' ' << utf8_to_wide(status.location->systemName);
        if (status.location->observedAt.time_since_epoch().count() != 0)
        {
            oss << L" (" << formatRelativeTime(std::optional<std::chrono::system_clock::time_point>{status.location->observedAt}) << L")";
        }
    }
    else
    {
    oss << L' ' << kBulletChar << L" awaiting Local chat";
    }

    if (!status.chatLogFile.empty())
    {
           oss << L' ' << kBulletChar << L' ' << formatPath(status.chatLogFile, 18);
    }

    return oss.str();
}

std::wstring HelperTrayApplication::formatCombatLine(const HelperRuntime::Status& status) const
{
    if (!status.combat)
    {
        return {};
    }

    std::wostringstream oss;
    oss << L"Combat: " << status.combat->combatEventCount << L" hits";
    if (status.combat->notifyEventCount > 0)
    {
        oss << L" / " << status.combat->notifyEventCount << L" notify";
    }
    if (!status.combat->characterId.empty())
    {
            oss << L' ' << kBulletChar << L' ' << utf8_to_wide(status.combat->characterId);
    }
    if (status.combat->lastEventAt.time_since_epoch().count() != 0)
    {
    oss << L' ' << kBulletChar << L' ' << formatRelativeTime(std::optional<std::chrono::system_clock::time_point>{status.combat->lastEventAt});
    }
    return oss.str();
}

std::wstring HelperTrayApplication::formatOverlayLine(const HelperRuntime::Status& status) const
{
    if (!status.hasOverlayState && !status.lastOverlayAcceptedAt && !status.lastOverlayGeneratedAt)
    {
        return {};
    }

    std::wostringstream oss;
    oss << L"Overlay:";
    if (status.hasOverlayState)
    {
        oss << L" ready";
    }
    if (status.lastOverlayGeneratedAt)
    {
           oss << L' ' << kBulletChar << L" gen " << formatRelativeTime(status.lastOverlayGeneratedAt);
    }
    if (status.lastOverlayAcceptedAt)
    {
           oss << L' ' << kBulletChar << L" ing " << formatRelativeTime(status.lastOverlayAcceptedAt);
    }
    return oss.str();
}

std::wstring HelperTrayApplication::formatTelemetryLine(const HelperRuntime::Status& status) const
{
    const auto& telemetry = status.telemetry;
    const bool hasCombat = telemetry.combat.has_value() && telemetry.combat->hasData();
    const bool hasMining = telemetry.mining.has_value() && telemetry.mining->hasData();
    const bool hasHistory = telemetry.history.has_value() && (telemetry.history->hasData() || telemetry.history->saturated);
    const bool hasReset = status.lastTelemetryResetAt.has_value();

    if (!hasCombat && !hasMining && !hasHistory && !hasReset)
    {
        return {};
    }

    std::wostringstream oss;
    oss << L"Telemetry:";

    bool firstSegment = true;
    auto appendSegment = [&](const std::wstring& segment) {
        if (segment.empty())
        {
            return;
        }
        if (!firstSegment)
        {
            oss << L"; ";
        }
        else
        {
            oss << L' ';
            firstSegment = false;
        }
        oss << segment;
    };

    if (hasCombat)
    {
        std::wstring segment = L"combat " + format_double(telemetry.combat->totalDamageDealt)
            + L" / " + format_double(telemetry.combat->totalDamageTaken);
        appendSegment(segment);
    }

    if (hasMining)
    {
        std::wstring segment = L"mining " + format_double(telemetry.mining->totalVolumeM3) + L" m3";
        if (!telemetry.mining->buckets.empty())
        {
            const auto& bucket = telemetry.mining->buckets.front();
            segment += L" (" + utf8_to_wide(bucket.resource);
            if (telemetry.mining->buckets.size() > 1)
            {
                segment += L"+" + std::to_wstring(telemetry.mining->buckets.size() - 1);
            }
            segment += L")";
        }
        if (telemetry.mining->sessionDurationSeconds > 0.0)
        {
            segment += L" [" + format_double(telemetry.mining->sessionDurationSeconds / 60.0) + L" min]";
        }
        appendSegment(segment);
    }

    if (hasHistory)
    {
        std::wstring segment = L"history " + std::to_wstring(telemetry.history->slices.size());
        if (telemetry.history->capacity > 0)
        {
            segment += L"/" + std::to_wstring(telemetry.history->capacity);
        }
        segment += L" slices";
        appendSegment(segment);
    }

    if (hasReset)
    {
        std::wstring segment = L"reset " + formatRelativeTime(status.lastTelemetryResetAt);
        appendSegment(segment);
    }

    return oss.str();
}

std::filesystem::path HelperTrayApplication::getConfigPath() const
{
    PWSTR rawPath = nullptr;
    std::filesystem::path result;
    if (SUCCEEDED(::SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_CREATE, nullptr, &rawPath)))
    {
        result = std::filesystem::path(rawPath) / L"EFOverlay" / L"config.json";
        ::CoTaskMemFree(rawPath);
    }
    else
    {
        result = std::filesystem::temp_directory_path() / L"EFOverlay" / L"config.json";
    }

    std::error_code ec;
    std::filesystem::create_directories(result.parent_path(), ec);
    return result;
}

bool HelperTrayApplication::loadConfig()
{
    const auto configPath = getConfigPath();
    std::ifstream file(configPath);
    if (!file.is_open())
    {
        // Config doesn't exist yet - use defaults
        return false;
    }

    try
    {
        std::string line;
        while (std::getline(file, line))
        {
            // Simple key-value parsing (debug_logging_enabled: true/false)
            if (line.find("\"debug_logging_enabled\"") != std::string::npos)
            {
                debugLoggingEnabled_ = (line.find("true") != std::string::npos);
            }
        }
        return true;
    }
    catch (...)
    {
        spdlog::warn("Failed to parse config file");
        return false;
    }
}

void HelperTrayApplication::saveConfig()
{
    const auto configPath = getConfigPath();
    std::ofstream file(configPath);
    if (!file.is_open())
    {
        spdlog::error("Failed to save config file: {}", configPath.string());
        return;
    }

    // Simple JSON format
    file << "{\n";
    file << "  \"debug_logging_enabled\": " << (debugLoggingEnabled_ ? "true" : "false") << "\n";
    file << "}\n";
    file.close();

    spdlog::debug("Config saved to: {}", configPath.string());
}

void HelperTrayApplication::toggleDebugLogging()
{
    debugLoggingEnabled_ = !debugLoggingEnabled_;

    // Update spdlog level dynamically
    if (debugLoggingEnabled_)
    {
        spdlog::set_level(spdlog::level::debug);
        spdlog::info("Debug logging ENABLED");
        postBalloon(L"Debug logging", L"Verbose logging enabled", NIIF_INFO);
    }
    else
    {
        spdlog::info("Debug logging DISABLED");
        spdlog::set_level(spdlog::level::info);
        postBalloon(L"Debug logging", L"Verbose logging disabled", NIIF_INFO);
    }

    // Persist to config
    saveConfig();
}

std::string HelperTrayApplication::sanitizePath(const std::string& path) const
{
    std::string result = path;
    
    // Replace username in paths: C:\Users\JohnDoe\... → C:\Users\<USER>\...
    std::size_t usersPos = result.find("\\Users\\");
    if (usersPos != std::string::npos)
    {
        std::size_t nameStart = usersPos + 7; // Length of "\Users\"
        std::size_t nameEnd = result.find("\\", nameStart);
        if (nameEnd != std::string::npos)
        {
            result.replace(nameStart, nameEnd - nameStart, "<USER>");
        }
    }
    
    // Also sanitize machine name in UNC paths: \\MACHINE\share → \\<MACHINE>\share
    if (result.size() >= 2 && result[0] == '\\' && result[1] == '\\')
    {
        std::size_t machineEnd = result.find("\\", 2);
        if (machineEnd != std::string::npos)
        {
            result.replace(2, machineEnd - 2, "<MACHINE>");
        }
    }
    
    return result;
}

std::string HelperTrayApplication::sanitizeJson(const std::string& json) const
{
    // For now, just redact common PII fields
    std::string result = json;
    
    // Simple string replacement for common patterns
    const std::vector<std::string> piiFields = {
        "characterName", "pilotName", "userName", 
        "currentSystem", "systemName", "coordinates"
    };
    
    for (const auto& field : piiFields)
    {
        std::size_t pos = 0;
        while ((pos = result.find("\"" + field + "\"", pos)) != std::string::npos)
        {
            // Find the value (after the colon)
            std::size_t colonPos = result.find(":", pos);
            if (colonPos != std::string::npos)
            {
                std::size_t valueStart = result.find("\"", colonPos);
                if (valueStart != std::string::npos)
                {
                    std::size_t valueEnd = result.find("\"", valueStart + 1);
                    if (valueEnd != std::string::npos)
                    {
                        result.replace(valueStart + 1, valueEnd - valueStart - 1, "REDACTED");
                        pos = valueEnd;
                        continue;
                    }
                }
            }
            pos += field.length();
        }
    }
    
    return result;
}

std::string HelperTrayApplication::generateSystemInfo() const
{
    std::ostringstream ss;
    
    ss << "=== EF-Map Overlay Debug Report ===" << std::endl;
    
    // Timestamp
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    ss << "Generated: " << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S") << std::endl;
    ss << std::endl;
    
    // Helper version
    ss << "Helper Version: 1.0.2" << std::endl;
    ss << "Overlay DLL: ef-overlay.dll" << std::endl;
    ss << std::endl;
    
    // Windows version
    ss << "OS: Windows" << std::endl;
    OSVERSIONINFOEXW osvi{};
    osvi.dwOSVersionInfoSize = sizeof(osvi);
    #pragma warning(push)
    #pragma warning(disable: 4996)
    ::GetVersionExW(reinterpret_cast<LPOSVERSIONINFOW>(&osvi));
    #pragma warning(pop)
    ss << "Build: " << osvi.dwBuildNumber << std::endl;
    ss << std::endl;
    
    // Process info
    ss << "Helper Process ID: " << ::GetCurrentProcessId() << std::endl;
    
    // Check if helper is elevated
    BOOL isElevated = FALSE;
    HANDLE token = nullptr;
    if (::OpenProcessToken(::GetCurrentProcess(), TOKEN_QUERY, &token))
    {
        TOKEN_ELEVATION elevation{};
        DWORD size = sizeof(elevation);
        if (::GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &size))
        {
            isElevated = elevation.TokenIsElevated;
        }
        ::CloseHandle(token);
    }
    ss << "Helper Elevated: " << (isElevated ? "Yes" : "No") << std::endl;
    
    // Session ID
    DWORD sessionId = 0;
    ::ProcessIdToSessionId(::GetCurrentProcessId(), &sessionId);
    ss << "Helper Session ID: " << sessionId << std::endl;
    ss << std::endl;
    
    // Game process info
    ss << "Game Process: (searching for exefile.exe...)" << std::endl;
    DWORD gamePid = 0;
    HANDLE snapshot = ::CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot != INVALID_HANDLE_VALUE)
    {
        PROCESSENTRY32W entry{};
        entry.dwSize = sizeof(entry);
        if (::Process32FirstW(snapshot, &entry))
        {
            do
            {
                if (_wcsicmp(entry.szExeFile, L"exefile.exe") == 0)
                {
                    gamePid = entry.th32ProcessID;
                    break;
                }
            } while (::Process32NextW(snapshot, &entry));
        }
        ::CloseHandle(snapshot);
    }
    
    if (gamePid != 0)
    {
        ss << "Game Process ID: " << gamePid << std::endl;
        
        // Check game elevation
        BOOL gameElevated = FALSE;
        HANDLE gameToken = nullptr;
        HANDLE gameHandle = ::OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, gamePid);
        if (gameHandle)
        {
            if (::OpenProcessToken(gameHandle, TOKEN_QUERY, &gameToken))
            {
                TOKEN_ELEVATION elevation{};
                DWORD size = sizeof(elevation);
                if (::GetTokenInformation(gameToken, TokenElevation, &elevation, sizeof(elevation), &size))
                {
                    gameElevated = elevation.TokenIsElevated;
                }
                ::CloseHandle(gameToken);
            }
            ::CloseHandle(gameHandle);
        }
        ss << "Game Elevated: " << (gameElevated ? "Yes" : "No") << std::endl;
        
        // Session ID
        DWORD gameSessionId = 0;
        ::ProcessIdToSessionId(gamePid, &gameSessionId);
        ss << "Game Session ID: " << gameSessionId << std::endl;
        
        // Check for elevation/session mismatch
        if (isElevated != gameElevated)
        {
            ss << "WARNING: Elevation mismatch detected!" << std::endl;
        }
        if (sessionId != gameSessionId)
        {
            ss << "WARNING: Session mismatch detected!" << std::endl;
        }
    }
    else
    {
        ss << "Game Process: Not running" << std::endl;
    }
    ss << std::endl;
    
    // HTTP server status
    const auto status = runtime_.getStatus();
    ss << "HTTP Server: " << (status.serverRunning ? "Running" : "Stopped") << std::endl;
    ss << "HTTP Port: " << runtime_.server().port() << std::endl;
    ss << std::endl;
    
    // Shared memory status
    ss << "Shared Memory: (attempting to detect...)" << std::endl;
    HANDLE shmHandle = ::OpenFileMappingW(FILE_MAP_READ, FALSE, L"Local\\EFOverlaySharedState");
    if (shmHandle)
    {
        ss << "Shared Memory Handle: EXISTS" << std::endl;
        ::CloseHandle(shmHandle);
    }
    else
    {
        ss << "Shared Memory Handle: NOT FOUND (error " << ::GetLastError() << ")" << std::endl;
    }
    ss << std::endl;
    
    // Runtime status summary
    ss << "=== Runtime Status ===" << std::endl;
    ss << "Overlay State: " << (status.hasOverlayState ? "Available" : "None") << std::endl;
    ss << "Events Recorded: " << status.eventsRecorded << std::endl;
    ss << "Events Buffered: " << status.eventsBuffered << std::endl;
    ss << "Events Dropped: " << status.eventsDropped << std::endl;
    
    if (status.location)
    {
        ss << "Current System: " << sanitizePath(status.location->systemName) << std::endl;
    }
    
    if (!status.lastErrorMessage.empty())
    {
        ss << std::endl << "=== Recent Errors ===" << std::endl;
        ss << sanitizePath(status.lastErrorMessage) << std::endl;
    }
    
    if (!status.lastInjectionMessage.empty())
    {
        ss << std::endl << "Last Injection Message:" << std::endl;
        ss << sanitizePath(status.lastInjectionMessage) << std::endl;
    }
    
    return ss.str();
}

void HelperTrayApplication::exportDebugLogs()
{
    try
    {
        // Flush current logs
        spdlog::default_logger()->flush();
        
        // Get timestamp for filename
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        std::ostringstream timestamp;
        timestamp << std::put_time(std::localtime(&time_t), "%Y-%m-%d_%H%M%S");
        
        // Get desktop path
        PWSTR desktopPath = nullptr;
        std::filesystem::path exportDir;
        if (SUCCEEDED(::SHGetKnownFolderPath(FOLDERID_Desktop, 0, nullptr, &desktopPath)))
        {
            exportDir = desktopPath;
            ::CoTaskMemFree(desktopPath);
        }
        else
        {
            exportDir = std::filesystem::current_path();
        }
        
        // Create export folder
        std::string folderName = "EFOverlay_Logs_" + timestamp.str();
        std::filesystem::path exportPath = exportDir / folderName;
        std::filesystem::create_directories(exportPath);
        
        // Generate system info
        std::string sysInfo = generateSystemInfo();
        std::ofstream sysInfoFile(exportPath / "system_info.txt");
        sysInfoFile << sysInfo;
        sysInfoFile.close();
        
        // Copy log files (if they exist)
        const auto logDir = resolve_log_directory();
        std::error_code ec;
        
        if (std::filesystem::exists(logDir, ec))
        {
            for (const auto& entry : std::filesystem::directory_iterator(logDir, ec))
            {
                if (entry.is_regular_file())
                {
                    const auto& filename = entry.path().filename();
                    
                    // Read file content
                    std::ifstream inFile(entry.path(), std::ios::binary);
                    if (inFile.is_open())
                    {
                        std::string content((std::istreambuf_iterator<char>(inFile)),
                                          std::istreambuf_iterator<char>());
                        inFile.close();
                        
                        // Sanitize content
                        content = sanitizePath(content);
                        
                        // Write sanitized content
                        std::ofstream outFile(exportPath / filename);
                        outFile << content;
                        outFile.close();
                    }
                }
            }
        }
        
        // Copy config file
        const auto configPath = getConfigPath();
        if (std::filesystem::exists(configPath))
        {
            std::filesystem::copy_file(configPath, exportPath / "config.json", 
                                      std::filesystem::copy_options::overwrite_existing, ec);
        }
        
        // Open Explorer to show the folder
        std::wstring explorerCmd = L"/select,\"" + (exportPath / "system_info.txt").wstring() + L"\"";
        ::ShellExecuteW(nullptr, L"open", L"explorer.exe", explorerCmd.c_str(), nullptr, SW_SHOWNORMAL);
        
        // Show success notification
        std::wstring message = L"Logs exported to:\n" + utf8_to_wide(folderName);
        postBalloon(L"Debug logs exported", message, NIIF_INFO);
        
        spdlog::info("Debug logs exported to: {}", exportPath.string());
    }
    catch (const std::exception& e)
    {
        spdlog::error("Failed to export debug logs: {}", e.what());
        postBalloon(L"Export failed", L"Unable to export debug logs", NIIF_ERROR);
    }
}

std::wstring HelperTrayApplication::formatRelativeTime(const std::optional<std::chrono::system_clock::time_point>& stamp) const
{
    if (!stamp)
    {
        return L"never";
    }

    const auto now = std::chrono::system_clock::now();
    auto delta = now - *stamp;
    const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(delta).count();

    if (seconds < 2)
    {
        return L"just now";
    }
    if (seconds < 60)
    {
        return std::to_wstring(seconds) + L"s ago";
    }

    const auto minutes = seconds / 60;
    if (minutes < 60)
    {
        return std::to_wstring(minutes) + L"m ago";
    }

    const auto hours = minutes / 60;
    if (hours < 24)
    {
        return std::to_wstring(hours) + L"h ago";
    }

    const auto days = hours / 24;
    return std::to_wstring(days) + L"d ago";
}

LRESULT CALLBACK HelperTrayApplication::WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    auto* app = fromWindow(hwnd);

    switch (msg)
    {
        case WM_CREATE:
        {
            auto* createStruct = reinterpret_cast<CREATESTRUCTW*>(lParam);
            auto* param = static_cast<HelperTrayApplication*>(createStruct->lpCreateParams);
            if (param)
            {
                param->hwnd_ = hwnd;
                param->setWindowInstance();
            }
            return 0;
        }
        case WM_COMMAND:
        {
            if (app)
            {
                app->handleCommand(static_cast<MenuId>(LOWORD(wParam)));
            }
            return 0;
        }
        case WM_TIMER:
        {
            if (app)
            {
                app->handleTimer(static_cast<UINT_PTR>(wParam));
            }
            return 0;
        }
        case kTrayMessage:
        {
            if (app)
            {
                app->handleTrayEvent(lParam);
            }
            return 0;
        }
        case WM_DESTROY:
        {
            if (app)
            {
                app->clearWindowInstance();
                app->removeTrayIcon();
            }
            ::PostQuitMessage(0);
            return 0;
        }
        default:
            break;
    }

    return ::DefWindowProcW(hwnd, msg, wParam, lParam);
}

HelperTrayApplication* HelperTrayApplication::fromWindow(HWND hwnd)
{
    return reinterpret_cast<HelperTrayApplication*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA));
}

void HelperTrayApplication::setWindowInstance()
{
    ::SetWindowLongPtrW(hwnd_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
}

void HelperTrayApplication::clearWindowInstance()
{
    ::SetWindowLongPtrW(hwnd_, GWLP_USERDATA, 0);
}
