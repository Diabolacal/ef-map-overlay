#pragma once

#include "helper_runtime.hpp"

#include <windows.h>
#include <shellapi.h>

#include <chrono>
#include <filesystem>
#include <optional>
#include <string>

class HelperTrayApplication
{
public:
    HelperTrayApplication(HINSTANCE instance, HelperRuntime& runtime);
    ~HelperTrayApplication();

    int run();

private:
    enum class MenuId : UINT
    {
        Start = 1001,
        Stop,
        SampleState,
        Inject,
        OpenLogs,
        CopyDiagnostics,
        Exit
    };

    static constexpr UINT kTrayMessage = WM_APP + 1;
    static constexpr UINT_PTR kStatusTimerId = 1;

    bool registerWindowClass();
    bool createWindow();
    void addTrayIcon();
    void removeTrayIcon();
    void refreshTrayIcon();
    void updateTooltip();
    void showContextMenu();
    void handleCommand(MenuId id);
    void handleTrayEvent(LPARAM lParam);
    void handleTimer(UINT_PTR id);
    void postBalloon(const std::wstring& title, const std::wstring& message, DWORD flags) const;
    std::wstring buildTooltip() const;
    std::wstring buildDiagnosticsText() const;
    void copyDiagnosticsToClipboard(const std::wstring& text) const;
    std::wstring formatPath(const std::filesystem::path& path, std::size_t maxLength) const;
    std::wstring formatLogWatcherLine(const HelperRuntime::Status& status) const;
    std::wstring formatCombatLine(const HelperRuntime::Status& status) const;
    std::wstring formatOverlayLine(const HelperRuntime::Status& status) const;
    std::wstring formatRelativeTime(const std::optional<std::chrono::system_clock::time_point>& stamp) const;
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    static HelperTrayApplication* fromWindow(HWND hwnd);

    void setWindowInstance();
    void clearWindowInstance();

    HINSTANCE hInstance_;
    HWND hwnd_{nullptr};
    NOTIFYICONDATAW iconData_{};
    HelperRuntime& runtime_;
    bool iconAdded_{false};
    UINT_PTR statusTimerId_{0};
};
