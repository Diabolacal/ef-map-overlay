#pragma once

#include <windows.h>

class OverlayHook {
public:
    static OverlayHook& instance();

    bool initialize(HMODULE module);
    void shutdown();

private:
    OverlayHook() = default;
    ~OverlayHook() = default;

    OverlayHook(const OverlayHook&) = delete;
    OverlayHook& operator=(const OverlayHook&) = delete;

    bool initialized_{false};
};
