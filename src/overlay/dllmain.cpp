#include "overlay_renderer.hpp"
#include "overlay_hook.hpp"

#include <windows.h>

namespace
{
    constexpr auto attach_msg = L"[ef-overlay] DLL attached to process.";
    constexpr auto detach_msg = L"[ef-overlay] DLL detached from process.";

    void log_line(const wchar_t* message)
    {
        ::OutputDebugStringW(message);
    }

    DWORD WINAPI initialize_overlay(LPVOID parameter)
    {
        auto module = static_cast<HMODULE>(parameter);
        ::OutputDebugStringA("[ef-overlay] initialize_overlay thread starting\n");
        OverlayRenderer::instance().initialize(module);
        OverlayHook::instance().initialize(module);
        ::OutputDebugStringA("[ef-overlay] initialize_overlay thread completed\n");
        return 0;
    }
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason_for_call, LPVOID)
{
    switch (reason_for_call)
    {
        case DLL_PROCESS_ATTACH:
            ::DisableThreadLibraryCalls(module);
            log_line(attach_msg);
            if (!::QueueUserWorkItem(initialize_overlay, module, WT_EXECUTEDEFAULT))
            {
                ::OutputDebugStringA("[ef-overlay] QueueUserWorkItem failed, running initialize inline\n");
                OverlayRenderer::instance().initialize(module);
            }
            break;
        case DLL_PROCESS_DETACH:
            OverlayHook::instance().shutdown();
            OverlayRenderer::instance().shutdown();
            log_line(detach_msg);
            break;
        default:
            break;
    }

    return TRUE;
}
