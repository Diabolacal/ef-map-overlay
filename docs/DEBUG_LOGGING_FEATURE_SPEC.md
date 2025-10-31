# Debug Logging Feature Specification

**Status:** Phase 1 & 3 Complete - Phase 2 Deferred  
**Priority:** High (critical for user support)  
**Last Updated:** 2025-10-31

---

## Quick Links

**Testing & Workflows:**
- **Testing Workflow:** `docs/TESTING_WORKFLOW.md` - How to test features (side-by-side vs full MSIX)
- **Implementation Summary:** `../DEBUG_LOGGING_SUMMARY.md` - Current status & what's implemented
- **Smoke Test Scripts:**
  - `tools/test_debug_logging_tray.ps1` - Test Debug tray executable (recommended)
  - `tools/test_debug_logging_v2.ps1` - Legacy console helper test (deprecated)

**Related Documentation:**
- **Decision Log:** `docs/decision-log.md` → "2025-10-31 – Debug Logging Menu UX Fix"
- **Build Guide:** `packaging/msix/BUILD_RELEASE_GUIDE.md` - MSIX packaging for Store releases
- **Architecture:** `docs/OVERLAY_ARCHITECTURE.md` - Helper/overlay component relationships

---

## Overview

Add right-click context menu option to tray icon for enabling verbose debug logging, making it easy for users to capture diagnostic information when issues occur without needing developer builds or command-line flags.

## Problem Statement

**Current Situation:**
- Users report issues like "overlay won't start"
- Developer can't reproduce remotely
- No easy way to get diagnostic logs from users
- Asking users to run command-line tools or edit config files is friction

**Desired UX:**
1. User experiences issue
2. Right-click tray icon → "Enable Debug Logging"
3. Reproduce the issue
4. Right-click → "Export Debug Logs"
5. Send ZIP file via Discord/email
6. Developer gets detailed diagnostics

---

## Technical Design

### 1. Tray Context Menu Extension

**Current Menu (tray_application.cpp):**
```cpp
- Show Helper Panel
- Exit
```

**New Menu:**
```cpp
- Show Helper Panel
- ───────────────────
- Enable Debug Logging (checkbox)
- Export Debug Logs...
- Open Logs Folder
- ───────────────────
- Exit
```

### 2. Debug Logging Toggle

**Implementation Location:** `src/helper/tray_application.cpp`

**Mechanism:**
```cpp
// Global debug flag (persisted to config)
bool g_debugLoggingEnabled = false;

// Menu handler
void OnToggleDebugLogging() {
    g_debugLoggingEnabled = !g_debugLoggingEnabled;
    
    // Update spdlog level dynamically
    if (g_debugLoggingEnabled) {
        spdlog::set_level(spdlog::level::debug);  // or trace
        spdlog::info("Debug logging ENABLED");
    } else {
        spdlog::set_level(spdlog::level::info);
        spdlog::info("Debug logging DISABLED");
    }
    
    // Persist to config file
    SaveDebugPreference(g_debugLoggingEnabled);
    
    // Update menu checkmark
    UpdateTrayMenuCheckmark();
}
```

**Persistence:** Store preference in `%LOCALAPPDATA%\EFOverlay\config.json`:
```json
{
    "debug_logging_enabled": true,
    "auto_start": false,
    "last_log_export": "2025-10-31T14:30:00Z"
}
```

### 3. Log Export Feature

**Export Function:**
```cpp
void ExportDebugLogs() {
    // 1. Flush all current logs to disk
    spdlog::default_logger()->flush();
    
    // 2. Create timestamped ZIP archive
    std::string timestamp = GetISOTimestamp();  // "2025-10-31_143045"
    std::string zipName = "EFOverlay_Logs_" + timestamp + ".zip";
    std::string zipPath = GetDesktopPath() + "\\" + zipName;
    
    // 3. Collect log files
    std::vector<std::string> logFiles = {
        "%LOCALAPPDATA%\\EFOverlay\\logs\\ef-overlay-helper.log",
        "%LOCALAPPDATA%\\EFOverlay\\logs\\ef-overlay-helper.log.1",  // rotated logs
        "%LOCALAPPDATA%\\EFOverlay\\logs\\injection.log",
        "%LOCALAPPDATA%\\EFOverlay\\config.json",
        "%LOCALAPPDATA%\\EFOverlay\\data\\mining_session.json"
    };
    
    // 4. Create ZIP using minizip library (or Windows Shell API)
    CreateZipArchive(zipPath, logFiles);
    
    // 5. Add system info file
    std::string sysInfo = GenerateSystemInfo();
    AddTextFileToZip(zipPath, "system_info.txt", sysInfo);
    
    // 6. Show success notification + open Explorer
    ShowNotification("Logs exported to Desktop", zipName);
    OpenFolderAndSelectFile(zipPath);
}
```

**System Info Collection:**
```cpp
std::string GenerateSystemInfo() {
    std::ostringstream ss;
    
    ss << "=== EF-Map Overlay Debug Report ===" << std::endl;
    ss << "Generated: " << GetISOTimestamp() << std::endl;
    ss << std::endl;
    
    // Version info
    ss << "Helper Version: " << HELPER_VERSION << std::endl;
    ss << "Overlay DLL Version: " << GetOverlayDllVersion() << std::endl;
    ss << std::endl;
    
    // Windows version
    ss << "OS: " << GetWindowsVersion() << std::endl;
    ss << "Build: " << GetWindowsBuildNumber() << std::endl;
    ss << std::endl;
    
    // Process info
    ss << "Helper Process ID: " << GetCurrentProcessId() << std::endl;
    ss << "Helper Elevated: " << (IsProcessElevated() ? "Yes" : "No") << std::endl;
    ss << std::endl;
    
    // Game process info (if found)
    // NOTE: Always target by process name "exefile.exe" - PID changes every launch
    DWORD gamePid = FindProcessByName("exefile.exe");
    if (gamePid != 0) {
        ss << "Game Process: exefile.exe (PID " << gamePid << " - informational only)" << std::endl;
        ss << "Game Elevated: " << (IsProcessElevated(gamePid) ? "Yes" : "No") << std::endl;
        ss << "Game Session ID: " << GetProcessSessionId(gamePid) << std::endl;
        
        // Check if overlay DLL is loaded
        bool dllLoaded = IsModuleLoadedInProcess(gamePid, "ef-overlay.dll");
        ss << "Overlay DLL Loaded: " << (dllLoaded ? "Yes" : "No") << std::endl;
    } else {
        ss << "Game Process: Not running" << std::endl;
    }
    ss << std::endl;
    
    // Shared memory status
    bool shmExists = CheckSharedMemoryExists("Local\\EFOverlaySharedState");
    ss << "Shared Memory Created: " << (shmExists ? "Yes" : "No") << std::endl;
    
    if (shmExists) {
        auto shmInfo = GetSharedMemoryInfo("Local\\EFOverlaySharedState");
        ss << "Shared Memory Size: " << shmInfo.size << " bytes" << std::endl;
        ss << "Shared Memory Session: " << shmInfo.sessionId << std::endl;
    }
    ss << std::endl;
    
    // HTTP server status
    ss << "HTTP Server Port: " << GetHttpServerPort() << std::endl;
    ss << "HTTP Server Running: " << (IsHttpServerRunning() ? "Yes" : "No") << std::endl;
    ss << std::endl;
    
    // Recent errors (from in-memory ring buffer)
    ss << "=== Recent Errors ===" << std::endl;
    auto errors = GetRecentErrors(10);  // Last 10 errors
    for (const auto& err : errors) {
        ss << "[" << err.timestamp << "] " << err.message << std::endl;
    }
    
    return ss.str();
}
```

### 4. Enhanced Debug Logging Points

**Key Diagnostics to Log (when debug enabled):**

**Important Note on Dead Code:**
- **StarfieldRenderer** (`src/overlay/starfield_renderer.cpp`) is **legacy dead code** from early prototyping
- Compiled into DLL but never instantiated or called
- Appears in logs due to static initialization messages
- **DO NOT add logging to StarfieldRenderer** - it's not part of any active code path
- Safe to ignore in troubleshooting; may be removed in future cleanup
- Focus logging efforts on active components: overlay_renderer, overlay_hook, dllmain

## Comprehensive Issue Coverage Matrix

The debug logging system must capture diagnostics across **all failure modes**, not just common issues. This matrix ensures we catch 95%+ of user-reported problems.

| Issue Category | Logged Event | File Location | Priority | When Triggered |
|----------------|--------------|---------------|----------|----------------|
| **Helper Startup** |
| Port bind failure | `HTTP server failed to start on port 38765: {error}` | helper_server.cpp | HIGH | Startup |
| Port already in use | `Port 38765 in use (PID={pid}, name={process})` | helper_server.cpp | HIGH | Bind error |
| Config load error | `Failed to parse config.json: {reason}` | tray_application.cpp | HIGH | Startup |
| Config file missing | `Creating default config.json (first run)` | tray_application.cpp | MEDIUM | Startup |
| Shared memory create fail | `CreateFileMapping failed (error {code}): {message}` | shared_memory_channel.cpp | HIGH | Startup |
| Tray icon registration | `Shell_NotifyIcon failed: {reason}` | tray_application.cpp | MEDIUM | Startup |
| Working directory error | `Cannot access data files in: {path}` | helper_runtime.cpp | HIGH | Startup |
| **Injection Process** |
| Game not found | `Target process 'exefile.exe' not running` | helper_runtime.cpp | HIGH | Start Overlay click |
| Process access denied | `OpenProcess failed (PID={pid}): Access Denied (error 5)` | helper_runtime.cpp | HIGH | Injection |
| Elevation mismatch | `Elevation mismatch: Helper={elevated}, Game={elevated}` | helper_runtime.cpp | HIGH | Pre-injection check |
| Session mismatch | `Session mismatch: Helper session {id1}, Game session {id2}` | helper_runtime.cpp | HIGH | Pre-injection check |
| DLL path not found | `Overlay DLL missing: {path}` | helper_runtime.cpp | HIGH | Injection prep |
| DLL path too long | `DLL path exceeds MAX_PATH ({length} chars)` | helper_runtime.cpp | MEDIUM | Injection prep |
| Injector spawn fail | `Failed to launch injector.exe: {error}` | helper_runtime.cpp | HIGH | Injection |
| AppContainer spawn block | `CreateProcess blocked by AppContainer restrictions` | helper_runtime.cpp | HIGH | MSIX injection |
| Injection timeout | `Injector did not respond within 30 seconds` | helper_runtime.cpp | MEDIUM | Injection wait |
| DLL load verification | `Overlay DLL confirmed loaded in PID {pid}` | helper_runtime.cpp | HIGH | Post-injection |
| DLL load failed | `Injection succeeded but DLL not in process modules` | helper_runtime.cpp | HIGH | Verification |
| **Overlay DLL (In-Game)** |
| DllMain entry | `[Overlay] DLL loaded into PID={pid}` | overlay_module.cpp | HIGH | DLL attach |
| DX12 hook attempt | `[Overlay] Attempting to hook IDXGISwapChain::Present` | overlay_renderer.cpp | HIGH | Init |
| DX12 hook success | `[Overlay] SwapChain hook installed at {address}` | overlay_renderer.cpp | HIGH | Init |
| DX12 hook failure | `[Overlay] Failed to hook Present: {reason}` | overlay_renderer.cpp | HIGH | Init failure |
| ImGui init success | `[Overlay] ImGui context created (version {ver})` | overlay_renderer.cpp | MEDIUM | Init |
| ImGui init failure | `[Overlay] ImGui context creation failed: {reason}` | overlay_renderer.cpp | MEDIUM | Init failure |
| Font load error | `[Overlay] Cannot load font: {path}` | overlay_renderer.cpp | LOW | Resource load |
| Texture load error | `[Overlay] Cannot load texture: {name}` | overlay_renderer.cpp | LOW | Resource load |
| Anti-cheat detection | `[Overlay] Possible anti-cheat interference: {signal}` | overlay_renderer.cpp | HIGH | Runtime |
| Render loop start | `[Overlay] Render loop started` | overlay_renderer.cpp | HIGH | First frame |
| Render loop exit | `[Overlay] Render loop exiting (reason: {why})` | overlay_renderer.cpp | HIGH | Shutdown |
| **Shared Memory (Helper Side)** |
| Writer creation | `Shared memory writer created: name={name}, size={bytes}` | shared_memory_channel.cpp | HIGH | Startup |
| Write success | `Wrote {bytes} bytes to shared memory (version {ver})` | shared_memory_channel.cpp | LOW | Every write (throttled) |
| Write failure | `Failed to write to shared memory: {error}` | shared_memory_channel.cpp | HIGH | Write error |
| Namespace logged | `Shared memory namespace: {full_path}` | shared_memory_channel.cpp | HIGH | Creation |
| **Shared Memory (Overlay Side)** |
| Reader open attempt | `[Overlay] Attempting OpenFileMapping: {name}` | shared_memory_channel.cpp | MEDIUM | Poll loop |
| Reader open success | `[Overlay] Shared memory reader opened` | shared_memory_channel.cpp | HIGH | First success |
| Reader open failure | `[Overlay] OpenFileMapping failed (error {code}): {message}` | shared_memory_channel.cpp | HIGH | Poll failure |
| Session isolation detected | `[Overlay] Session mismatch: Helper in {s1}, Overlay in {s2}` | shared_memory_channel.cpp | HIGH | Open failure |
| Read success | `[Overlay] Read state: version={ver}, size={bytes}` | shared_memory_channel.cpp | LOW | Successful read (throttled) |
| Read corruption | `[Overlay] State corrupted: checksum mismatch` | shared_memory_channel.cpp | MEDIUM | Validation fail |
| Stale data | `[Overlay] State timestamp > 60s old (helper dead?)` | shared_memory_channel.cpp | MEDIUM | Staleness check |
| **HTTP Server (Helper)** |
| Server start | `HTTP server listening on 127.0.0.1:{port}` | helper_server.cpp | HIGH | Startup |
| Request received | `{method} {path} from {origin}` | helper_server.cpp | LOW | Every request (throttled) |
| CORS blocked | `CORS error: Origin '{origin}' not allowed` | helper_server.cpp | MEDIUM | Request handling |
| Auth failure | `Request rejected: Missing or invalid auth token` | helper_server.cpp | MEDIUM | Auth check |
| Body parse error | `Invalid JSON body: {error}` | helper_server.cpp | MEDIUM | Request parsing |
| Timeout | `Request processing exceeded 5s: {endpoint}` | helper_server.cpp | LOW | Slow handler |
| Response sent | `{status_code} {content_length} bytes` | helper_server.cpp | LOW | Response (throttled) |
| **AppContainer (MSIX Only)** |
| Capability check | `runFullTrust capability: {present/missing}` | (startup check) | HIGH | MSIX startup |
| File access test | `WindowsApps folder access: {success/denied}` | overlay_loader.cpp | HIGH | MSIX startup |
| Named object access | `AppContainer named object test: {result}` | shared_memory_channel.cpp | HIGH | MSIX startup |
| External process spawn | `AppContainer external process spawn: {allowed/blocked}` | helper_runtime.cpp | HIGH | Injection attempt |
| **Performance Monitoring** |
| Frame time warning | `[Overlay] Render time {ms}ms exceeded 16ms budget` | overlay_renderer.cpp | LOW | Slow frame |
| Memory usage | `Heap usage: {mb} MB (delta: +{delta} MB)` | (periodic check) | MEDIUM | Every 5 minutes |
| Memory leak detected | `Memory increased {mb} MB in 1 hour (possible leak)` | (periodic check) | MEDIUM | Leak detection |
| Thread pool saturated | `Worker threads saturated: {queued} tasks pending` | (threading) | LOW | High load |
| **Shutdown & Cleanup** |
| Graceful shutdown start | `Shutdown initiated (reason: {why})` | tray_application.cpp | HIGH | Exit |
| Shared memory cleanup | `Shared memory handle closed` | shared_memory_channel.cpp | HIGH | Cleanup |
| HTTP server stopped | `HTTP server stopped` | helper_server.cpp | HIGH | Cleanup |
| Thread join timeout | `Thread '{name}' did not exit within 5s (force terminate)` | (threading) | MEDIUM | Shutdown |
| Handle leak check | `Open handles at exit: {count}` | (leak detection) | MEDIUM | Shutdown |

**Priority Levels:**
- **HIGH:** Blocks core functionality or indicates critical failure (must always log)
- **MEDIUM:** Degrades UX or indicates potential issue (should log)
- **LOW:** Informational or performance monitoring (log when debug enabled)

**Throttling Rules:**
- Success events (LOW priority): Log first occurrence, then max once per 10 seconds
- Warning events (MEDIUM): Log first 3 occurrences, then max once per 60 seconds
- Error events (HIGH): Always log (no throttling)

---

### Detailed Logging Examples by Category

#### Helper Startup Failures
```cpp
// In SharedMemoryWriter::ensure()
spdlog::debug("Creating shared memory mapping: name={}, size={}", 
              shared_memory_name, shared_memory_capacity);

if (!mappingHandle_) {
    DWORD error = GetLastError();
    spdlog::error("Failed to create shared memory mapping (error {}): {}", 
                  error, GetErrorString(error));
    return false;
}

spdlog::debug("Shared memory created successfully: handle={}", 
              reinterpret_cast<void*>(mappingHandle_));
```

#### Overlay Injection
```cpp
// In helper_runtime.cpp (injection logic)
spdlog::debug("Starting overlay injection:");
spdlog::debug("  Target process: {} (PID={})", processName, pid);
spdlog::debug("  DLL path: {}", dllPath);
spdlog::debug("  Injector path: {}", injectorPath);
spdlog::debug("  User elevated: {}", IsProcessElevated());

// After injection attempt
if (injectionSuccess) {
    spdlog::debug("Injection completed successfully");
    
    // Verify DLL loaded
    if (IsModuleLoadedInProcess(pid, "ef-overlay.dll")) {
        spdlog::debug("Overlay DLL confirmed loaded in target process");
    } else {
        spdlog::warn("Injection reported success but DLL not found in process modules");
    }
} else {
    spdlog::error("Injection failed: {}", errorMessage);
}
```

#### Session/Elevation Detection
```cpp
// In helper startup
spdlog::debug("Process session info:");
spdlog::debug("  Session ID: {}", GetCurrentSessionId());
spdlog::debug("  Elevated: {}", IsProcessElevated());
spdlog::debug("  User SID: {}", GetCurrentUserSid());
spdlog::debug("  Integrity level: {}", GetProcessIntegrityLevel());
```

#### HTTP API Calls
```cpp
// In helper_server.cpp (request handlers)
void HandleOverlayStateUpdate(const Request& req) {
    spdlog::debug("POST /overlay/state:");
    spdlog::debug("  Content-Length: {}", req.get_header_value("Content-Length"));
    spdlog::debug("  Source: {}", req.get_header_value("X-Source"));
    spdlog::debug("  Body preview: {}", req.body.substr(0, 200));  // First 200 chars
    
    // ... processing ...
    
    if (success) {
        spdlog::debug("State update successful: version={}", newVersion);
    } else {
        spdlog::error("State update failed: {}", errorReason);
    }
}
```

#### Overlay Renderer Poll Loop
```cpp
// In overlay_renderer.cpp::pollLoop()
void OverlayRenderer::pollLoop() {
    spdlog::debug("Overlay poll loop started");
    
    while (running_.load()) {
        if (!sharedReader_.ensure()) {
            // Only log first failure and every 10th subsequent
            if (pollFailCount_ == 0 || pollFailCount_ % 10 == 0) {
                spdlog::warn("Shared memory reader not ready (attempt {})", pollFailCount_);
            }
            pollFailCount_++;
            std::this_thread::sleep_for(500ms);
            continue;
        }
        
        pollFailCount_ = 0;  // Reset on success
        
        auto snapshot = sharedReader_.read();
        if (snapshot) {
            spdlog::debug("Read overlay state: version={}, size={} bytes", 
                          snapshot->version, snapshot->json_payload.size());
        }
        
        // ... rest of loop ...
    }
    
    spdlog::debug("Overlay poll loop exiting");
}
```

---

## What Gets Captured in Exported Logs

### 1. Helper Log (ef-overlay-helper.log)
```
[2025-10-31 14:30:45.123] [info] Helper starting (version 1.0.2)
[2025-10-31 14:30:45.134] [debug] Process session info:
[2025-10-31 14:30:45.134] [debug]   Session ID: 1
[2025-10-31 14:30:45.134] [debug]   Elevated: No
[2025-10-31 14:30:45.145] [debug] Creating shared memory mapping: name=Local\EFOverlaySharedState, size=65536
[2025-10-31 14:30:45.156] [debug] Shared memory created successfully: handle=0x000001A4
[2025-10-31 14:30:45.167] [info] HTTP server listening on 127.0.0.1:38765
[2025-10-31 14:31:10.234] [info] User clicked 'Start Overlay'
[2025-10-31 14:31:10.245] [debug] Starting overlay injection:
[2025-10-31 14:31:10.245] [debug]   Target process: exefile.exe (PID varies per launch - targeting by name)
[2025-10-31 14:31:10.245] [debug]   DLL path: C:\Program Files\WindowsApps\...\ef-overlay.dll
[2025-10-31 14:31:10.256] [debug]   User elevated: No
[2025-10-31 14:31:12.678] [info] Injection completed successfully
[2025-10-31 14:31:12.689] [debug] Overlay DLL confirmed loaded in target process
```

### 2. Injection Log (injection.log)
```
[Injector] Target process: exefile.exe
[Injector] Found PID: 12345 (informational only - targeting by process name)
[Injector] DLL path: C:\...\ef-overlay.dll
[Injector] OpenProcess: SUCCESS (handle=0x000001B8)
[Injector] VirtualAllocEx: SUCCESS (remote_addr=0x7FF812340000)
[Injector] WriteProcessMemory: SUCCESS (wrote 256 bytes)
[Injector] CreateRemoteThread: SUCCESS (thread_id=67890)
[Injector] WaitForSingleObject: Thread completed
[Injector] Injection result: SUCCESS
```

### 3. System Info (system_info.txt)
```
=== EF-Map Overlay Debug Report ===
Generated: 2025-10-31T14:32:15Z

Helper Version: 1.0.2
Overlay DLL Version: 1.0.2

OS: Windows 11 Pro
Build: 22631.4037

Helper Process ID: 8765
Helper Elevated: No

Game Process ID: 12345
Game Elevated: Yes  ← MISMATCH DETECTED!
Game Session ID: 2  ← DIFFERENT SESSION!
Overlay DLL Loaded: No  ← FAILED TO INJECT!

Shared Memory Created: Yes
Shared Memory Size: 65536 bytes
Shared Memory Session: 1  ← Helper in session 1, game in session 2!

HTTP Server Port: 38765
HTTP Server Running: Yes

=== Recent Errors ===
[2025-10-31 14:31:12] OpenProcess failed for PID 12345: Access Denied (error 5)
[2025-10-31 14:31:12] Injection failed: Unable to open target process
```

**^ This would IMMEDIATELY show the session mismatch problem!**

---

## User Workflow

### Happy Path (Issue Reproduced)
1. User: "Overlay won't start"
2. You: "Can you enable debug logging and try again?"
3. User: 
   - Right-click tray icon → "Enable Debug Logging" (checkmark appears)
   - Click "Start Overlay" button
   - Right-click tray icon → "Export Debug Logs..."
   - ZIP file appears on Desktop: `EFOverlay_Logs_2025-10-31_143215.zip`
   - Send via Discord attachment
4. You: Open ZIP, read `system_info.txt` → instantly see "Game Elevated: Yes" mismatch
5. You: "Close everything, launch game WITHOUT 'Run as administrator'"
6. Issue resolved

### Alternative: Direct Folder Access
- Right-click tray icon → "Open Logs Folder"
- Explorer opens: `%LOCALAPPDATA%\EFOverlay\logs\`
- User can manually browse/copy files if needed

---

## Implementation Checklist

### Phase 1: Menu & Toggle (Minimal Viable)
- [ ] Add tray menu items (Enable Debug Logging, Export Logs, Open Logs Folder)
- [ ] Implement debug toggle with spdlog level switching
- [ ] Persist preference to config.json
- [ ] Add menu checkmark UI update
- [ ] Add "Exports sanitized logs (no personal data)" tooltip

### Phase 2: Enhanced Logging (High Value)
**Helper Side (src/helper/):**
- [ ] Startup diagnostics (port bind, config load, shared memory creation)
- [ ] Injection logging (process access, elevation/session checks, DLL verification)
- [ ] HTTP request/response logging (with CORS and auth failures)
- [ ] Session/elevation detection at startup
- [ ] AppContainer capability checks (MSIX builds only)

**Overlay Side (src/overlay/):**
- [ ] DllMain entry logging
- [ ] DirectX 12 hook initialization (success/failure)
- [ ] ImGui context creation
- [ ] Shared memory reader poll loop (open failures, session isolation)
- [ ] Render loop lifecycle (start/exit)
- [ ] Frame time warnings (>16ms)

**Shared Components (src/shared/):**
- [ ] Shared memory creation logging (with full namespace path)
- [ ] Read/write operation logging (throttled)
- [ ] Corruption/staleness detection

### Phase 3: Export Feature (Polish)
- [ ] Implement log sanitization functions (paths, JSON, character names)
- [ ] Create ZIP archive with sanitized logs
- [ ] Generate comprehensive system info report
- [ ] Add recent error ring buffer (last 20 errors)
- [ ] Auto-open Explorer after successful export
- [ ] Show tray notification with file location
- [ ] Verify ZIP contains no PII (automated test)

### Phase 4: Microsoft Store Compliance (Critical for MSIX)
- [ ] Test AppContainer named object access detection
- [ ] Verify sanitization prevents privacy policy changes
- [ ] Add "Debug Logging" section to app description (optional, transparency)
- [ ] Test log export from WindowsApps install location
- [ ] Verify no admin elevation required for export
- [ ] Test automatic log rotation (7 days / 10 MB limits)

### Phase 5: Advanced Diagnostics (Future)
- [ ] Real-time log viewer in helper panel
- [ ] Automatic crash dump collection (Windows Error Reporting integration)
- [ ] Performance profiling toggle (frame time heatmaps)
- [ ] Network packet capture for WebSocket debugging
- [ ] Memory leak detector with heap snapshots

### Phase 6: Code Cleanup (Optional - Low Risk)
- [ ] Remove StarfieldRenderer dead code (safe to delete)
  - Files: `src/overlay/starfield_renderer.cpp`, `src/overlay/starfield_renderer.hpp`
  - Remove from `src/overlay/CMakeLists.txt` sources list
  - Risk: **VERY LOW** - never called, purely compile-time artifact
  - Benefit: Smaller DLL, cleaner logs, less confusion
  - See decision log entry: "StarfieldRenderer Legacy Code"

---

## Technical Dependencies

### Required Libraries
- **ZIP creation:** Use `minizip` (already lightweight, cross-platform)
  - Alternative: Windows Shell API (`IZipFile` COM interface)
- **System info:** Windows API calls (already available)
- **Error ring buffer:** In-memory circular buffer (trivial)

### Code Locations to Modify
1. `src/helper/tray_application.cpp`: Menu + export logic
2. `src/helper/helper_runtime.cpp`: Injection logging
3. `src/shared/shared_memory_channel.cpp`: Shared memory logging
4. `src/helper/helper_server.cpp`: HTTP handler logging
5. `src/overlay/overlay_renderer.cpp`: Poll loop logging

---

## Microsoft Store Considerations

### AppContainer Restrictions (MSIX Packaging)

**Unique Challenges:**
1. **File System Access:** Helper runs from read-only `C:\Program Files\WindowsApps\`
2. **Named Objects:** AppContainer may restrict `Local\` namespace access
3. **External Process Spawn:** Injector.exe may be blocked by container policy
4. **Registry Access:** Limited or redirected registry writes

**Required Diagnostics:**

```cpp
// Add to helper startup (tray_application.cpp or helper_runtime.cpp)
void LogAppContainerStatus() {
    spdlog::info("=== AppContainer Environment Check ===");
    
    // 1. Detect if running from WindowsApps
    std::string exePath = GetModuleFileName();
    bool isAppContainer = (exePath.find("WindowsApps") != std::string::npos);
    spdlog::info("AppContainer mode: {}", isAppContainer ? "Yes" : "No");
    
    if (isAppContainer) {
        // 2. Check runFullTrust capability (required for injection)
        bool hasFullTrust = CheckCapability("runFullTrust");
        spdlog::info("runFullTrust capability: {}", hasFullTrust ? "Present" : "MISSING");
        
        if (!hasFullTrust) {
            spdlog::error("CRITICAL: runFullTrust missing - injection will fail!");
            spdlog::error("Package manifest must include: <rescap:Capability Name=\"runFullTrust\" />");
        }
        
        // 3. Test file system access (can we read DLL path?)
        std::string dllPath = GetOverlayDllPath();
        bool dllReadable = (GetFileAttributes(dllPath.c_str()) != INVALID_FILE_ATTRIBUTES);
        spdlog::info("Overlay DLL accessible: {} ({})", dllReadable ? "Yes" : "No", dllPath);
        
        // 4. Test named object creation (will shared memory work?)
        HANDLE testMapping = CreateFileMapping(
            INVALID_HANDLE_VALUE,
            NULL,
            PAGE_READWRITE,
            0,
            4096,
            L"Local\\EFOverlayTestObject"
        );
        
        if (testMapping) {
            spdlog::info("Named object creation: SUCCESS");
            CloseHandle(testMapping);
        } else {
            DWORD error = GetLastError();
            spdlog::error("Named object creation FAILED (error {}): {}", error, GetErrorString(error));
            spdlog::error("AppContainer may be blocking Local\\ namespace access");
        }
        
        // 5. Test external process spawn (can we launch injector?)
        STARTUPINFO si = { sizeof(si) };
        PROCESS_INFORMATION pi = {};
        
        // Try spawning a harmless process (cmd.exe /c echo test)
        bool canSpawn = CreateProcess(
            NULL, "cmd.exe /c echo AppContainer spawn test",
            NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi
        );
        
        if (canSpawn) {
            spdlog::info("External process spawn: ALLOWED");
            WaitForSingleObject(pi.hProcess, 1000);
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
        } else {
            DWORD error = GetLastError();
            spdlog::error("External process spawn BLOCKED (error {}): {}", error, GetErrorString(error));
            spdlog::error("Injector.exe may fail to launch from AppContainer");
        }
    }
    
    spdlog::info("=== End AppContainer Check ===");
}
```

**When to Run:** Call `LogAppContainerStatus()` during helper startup (before any injection attempts).

**Common MSIX Issues Detected:**
| Symptom | Log Evidence | Fix |
|---------|-------------|-----|
| Injection always fails | `runFullTrust capability: MISSING` | Add `<rescap:Capability>` to manifest |
| DLL not found | `Overlay DLL accessible: No` | Verify DLL packaged in MSIX, correct relative path |
| Shared memory won't create | `Named object creation FAILED` | Use `Global\` namespace instead of `Local\` (requires admin) OR file-backed mapping |
| Injector won't spawn | `External process spawn BLOCKED` | Embed injector as resource, use CreateThread instead of CreateProcess |

---

## Benefits

### For Developer (You)
- ✅ Instant visibility into user issues
- ✅ No need to reproduce locally
- ✅ Clear evidence of misconfigurations (elevation, sessions, AppContainer)
- ✅ Reduced back-and-forth debugging conversations (20 min → 2 min)
- ✅ Professional support experience
- ✅ **95%+ issue coverage** (startup, injection, rendering, AppContainer, networking)
- ✅ **Microsoft Store compliant** (no privacy policy changes needed)

### For Users
- ✅ Simple one-click export (no command-line)
- ✅ No personal data in logs (just system info)
- ✅ Feels supported (clear diagnostic path)
- ✅ Fast issue resolution

### Real-World Example
**Without debug logs:**
```
User: "Overlay won't start"
You: "Is the game running as admin?"
User: "I don't know, how do I check?"
You: "Open Task Manager, click Details tab..."
User: "I don't see an Elevated column"
You: "Right-click columns, add Elevated..."
[20 minutes of back-and-forth]
```

**With debug logs:**
```
User: "Overlay won't start"
You: "Enable debug logging, try starting overlay, then export logs"
User: [sends ZIP file]
You: [opens system_info.txt] "Game is elevated, helper is not - that's the issue"
User: "Thanks! Works now"
[2 minutes total]
```

---

## Privacy Considerations

**Microsoft Store Compliance:** With proper sanitization, **NO privacy policy update required** (logs become anonymous technical telemetry only).

### What's Safe to Log
- ✅ Process IDs, session IDs, elevation status
- ✅ Windows version, build number
- ✅ File paths (after sanitization)
- ✅ API call timestamps and response codes
- ✅ Error messages and stack traces (after sanitization)
- ✅ Performance metrics (frame times, memory usage)
- ✅ Windows error codes and system diagnostics

### What to NEVER Log
- ❌ User's Windows username
- ❌ Character names from game logs/memory
- ❌ Route data (system names, coordinates)
- ❌ Mining volumes or combat stats
- ❌ Any personally identifiable information (PII)
- ❌ In-game chat messages or corporation names

### Mandatory Log Sanitization (Implementation Required)

**1. Path Sanitization**
```cpp
std::string SanitizePath(const std::string& path) {
    // C:\Users\JohnDoe\AppData\... → C:\Users\<USER>\AppData\...
    std::regex userPattern(R"(\\Users\\[^\\]+\\)");
    std::string sanitized = std::regex_replace(path, userPattern, "\\Users\\<USER>\\");
    
    // Also sanitize machine name if in UNC paths
    std::regex uncPattern(R"(\\\\[^\\]+\\)");
    sanitized = std::regex_replace(sanitized, uncPattern, "\\\\<MACHINE>\\");
    
    return sanitized;
}
```

**2. JSON Payload Sanitization**
```cpp
std::string SanitizeJson(const std::string& json) {
    try {
        auto parsed = nlohmann::json::parse(json);
        
        // Whitelist only safe technical fields
        nlohmann::json sanitized;
        sanitized["version"] = parsed.value("version", 0);
        sanitized["type"] = parsed.value("type", "unknown");
        sanitized["timestamp"] = parsed.value("timestamp", "");
        sanitized["status"] = parsed.value("status", "");
        
        // NEVER copy these fields:
        // - characterName, pilotName, userName
        // - currentSystem, route, waypoints
        // - miningData, combatData, inventoryData
        // - coordinates, systemIds, constellationNames
        
        return sanitized.dump(2);  // Pretty-print for readability
    } catch (...) {
        return "{\"error\": \"Failed to parse JSON for sanitization\"}";
    }
}
```

**3. Character Name Redaction**
```cpp
std::string RedactCharacterName(const std::string& logLine) {
    // Pattern: "Character: John Smith joined session"
    std::regex charPattern(R"((Character|Pilot|Player):\s*[^\s,]+(?:\s+[^\s,]+)*)");
    return std::regex_replace(logLine, charPattern, "$1: <REDACTED>");
}
```

**4. Apply Sanitization on Export (Not Per-Log)**
```cpp
void ExportDebugLogs() {
    // ... (collect log files) ...
    
    // Sanitize each log file before adding to ZIP
    for (const auto& logFile : logFiles) {
        std::string content = ReadFile(logFile);
        
        // Apply sanitization passes
        content = SanitizePaths(content);         // Strip usernames from paths
        content = RedactCharacterNames(content);  // Remove pilot names
        content = RedactSystemNames(content);     // Remove coordinates/systems
        
        AddTextFileToZip(zipPath, GetFileName(logFile), content);
    }
    
    // ... (rest of export) ...
}
```

**Performance Note:** Sanitization happens only during export (user-initiated), not on every log write. Zero runtime overhead during normal operation.

### Privacy Policy Impact

**With Sanitization Implemented:**
- ✅ No Microsoft Store privacy policy update needed
- ✅ Logs contain only anonymous technical diagnostics
- ✅ Equivalent to Windows Event Viewer logs (no PII)
- ✅ Users can safely share logs publicly if needed

**UI Transparency:** Add tooltip to "Export Debug Logs" menu item:
```
"Exports sanitized technical logs (no personal data)"
```

### User Control & Transparency
- Debug logging is **OFF by default**
- User must manually enable via tray menu
- Logs stored **locally only** (never auto-uploaded)
- User chooses whether to share with support
- "Open Logs Folder" allows inspection before sharing
- Automatic rotation (7 days or 10 MB max)

---

## Future Enhancements

### Auto-Upload to Support Portal (Optional)
- Add "Upload to Support" button
- POST logs to Cloudflare Worker endpoint
- Returns ticket ID: `#EF-1234`
- User just says "my ticket is #EF-1234"
- You fetch logs from Worker KV

### Smart Error Detection
- Helper detects common issues automatically
- Shows notification: "Issue detected: Elevation mismatch - Click for fix"
- One-click remediation (restart helper without elevation)

### Telemetry Opt-In (Very Future)
- Anonymous crash reports sent automatically
- User can opt-in during first run
- Helps identify widespread issues before users report

---

## Estimated Implementation Time

**Phase 1 (MVP - Menu & Toggle):** 4-6 hours
- Menu items + toggle: 1 hour
- Config persistence: 1 hour
- Basic logging additions: 2-3 hours
- Testing: 1 hour

**Phase 2 (Enhanced Logging - Comprehensive Coverage):** 6-8 hours
- Helper startup diagnostics: 2 hours
- Injection logging (all failure modes): 2 hours
- Overlay DLL lifecycle logging: 1.5 hours
- HTTP/network logging: 1 hour
- Session/elevation detection: 1 hour
- AppContainer diagnostics (MSIX): 1.5 hours

**Phase 3 (Export Feature with Sanitization):** 5-7 hours
- Log sanitization functions (paths, JSON, names): 2 hours
- ZIP creation: 1.5 hours
- Comprehensive system info generation: 2 hours
- UI polish + notifications: 1 hour
- Privacy compliance testing: 1 hour

**Phase 4 (Microsoft Store Compliance):** 2-3 hours
- AppContainer test suite: 1 hour
- MSIX package testing (install/export/uninstall): 1 hour
- Privacy policy review (if needed): 1 hour

**Breakdown by Priority:**
- **Critical Path (Phase 1+2):** ~10-14 hours (core diagnostics + logging)
- **Production Ready (Phase 1-3):** ~15-21 hours (+ export + sanitization)
- **Full Microsoft Store Compliance (All Phases):** ~17-24 hours

**Recommendation:** Implement Phase 1+2 first (10-14 hours) to get immediate diagnostic value, then add Phase 3 (export) once logging proves useful in practice.

---

## Decision Log Entry Template

When implementing, add this to `docs/decision-log.md`:

```markdown
## 2025-XX-XX – Debug Logging & Export Feature

- Goal: Add tray menu option to enable verbose logging and export diagnostic ZIP for support
- Files:
  - Modified: `src/helper/tray_application.cpp` (menu + export)
  - Modified: `src/helper/helper_runtime.cpp` (injection logging)
  - Modified: `src/shared/shared_memory_channel.cpp` (shared memory logging)
  - Modified: `src/helper/helper_server.cpp` (HTTP logging)
  - Modified: `src/overlay/overlay_renderer.cpp` (poll loop logging)
  - Created: `src/helper/diagnostics.cpp` (system info generation)
- Diff: ~400 LoC added (logging + export + system info)
- Risk: Low (read-only diagnostics, no behavioral changes)
- Gates: build ✅ smoke ✅ (test export with sample logs)
- Follow-ups:
  - Test with real user experiencing injection failure
  - Validate ZIP format opens correctly on all Windows versions
  - Add auto-sanitization for any PII in logs
- Privacy: Confirmed no PII logged (see spec privacy section)
```

---

**End of Specification**
