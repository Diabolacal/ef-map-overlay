#pragma once

#include <string>

// Registers the ef-overlay:// URL protocol for the current user.
// The handler command will point to the provided executable path.
bool register_overlay_protocol(const std::wstring& executablePath);

// Removes the ef-overlay:// URL protocol registration for the current user.
bool unregister_overlay_protocol();
