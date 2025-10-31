# Debug Logging Feature Specification

**Status:** Design Ready - Implementation Deferred  
**Priority:** High (critical for user support)  
**Last Updated:** 2025-10-31

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
    DWORD gamePid = FindProcessByName("exefile.exe");
    if (gamePid != 0) {
        ss << "Game Process ID: " << gamePid << std::endl;
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

#### Shared Memory Operations
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
[2025-10-31 14:31:10.245] [debug]   Target process: exefile.exe (PID=12345)
[2025-10-31 14:31:10.245] [debug]   DLL path: C:\Program Files\WindowsApps\...\ef-overlay.dll
[2025-10-31 14:31:10.256] [debug]   User elevated: No
[2025-10-31 14:31:12.678] [info] Injection completed successfully
[2025-10-31 14:31:12.689] [debug] Overlay DLL confirmed loaded in target process
```

### 2. Injection Log (injection.log)
```
[Injector] Target PID: 12345
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
- [ ] Add tray menu items (Enable Debug Logging, Open Logs Folder)
- [ ] Implement debug toggle with spdlog level switching
- [ ] Persist preference to config.json
- [ ] Add menu checkmark UI update

### Phase 2: Enhanced Logging (High Value)
- [ ] Add debug logs to shared memory creation
- [ ] Add debug logs to injection process
- [ ] Add debug logs to HTTP request handlers
- [ ] Add debug logs to overlay poll loop
- [ ] Add session/elevation detection logs

### Phase 3: Export Feature (Polish)
- [ ] Implement log export function (ZIP creation)
- [ ] Add system info generation
- [ ] Add recent error ring buffer
- [ ] Add Explorer auto-open after export
- [ ] Add tray notification on export success

### Phase 4: Advanced Diagnostics (Future)
- [ ] Real-time log viewer in helper panel
- [ ] Automatic crash dump collection
- [ ] Performance profiling toggle
- [ ] Network packet capture (for WebSocket debugging)

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

## Benefits

### For Developer (You)
- ✅ Instant visibility into user issues
- ✅ No need to reproduce locally
- ✅ Clear evidence of misconfigurations (elevation, sessions)
- ✅ Reduced back-and-forth debugging conversations
- ✅ Professional support experience

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

**What's Safe to Log:**
- ✅ Process IDs, session IDs, elevation status
- ✅ Windows version, build number
- ✅ File paths (helper/DLL locations)
- ✅ API call timestamps and response codes
- ✅ Error messages and stack traces

**What to NEVER Log:**
- ❌ User's Windows username
- ❌ Character names from game logs
- ❌ Route data (system names, coordinates)
- ❌ Mining volumes or combat stats
- ❌ Any personally identifiable information

**Log Sanitization:**
- Replace username in paths: `C:\Users\JohnDoe\...` → `C:\Users\<USER>\...`
- Redact character names if parsing game logs
- Strip any coordinates or system names from payloads

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

**Phase 1 (MVP):** 4-6 hours
- Menu items + toggle: 1 hour
- Config persistence: 1 hour
- Basic logging additions: 2-3 hours
- Testing: 1 hour

**Phase 2 (Enhanced Logging):** 2-3 hours
- Session/elevation detection: 1 hour
- Comprehensive log points: 1-2 hours

**Phase 3 (Export Feature):** 3-4 hours
- ZIP creation: 1-2 hours
- System info generation: 1 hour
- UI polish: 1 hour

**Total MVP (Phase 1+2):** ~6-9 hours  
**Complete Feature (All Phases):** ~9-13 hours

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
