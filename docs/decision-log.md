# Technical Decision Log (EF-Map Overlay)

## 2025-10-31 – Store Submission v1.0.2 Publisher Validation Error (Packaging Script Bug)
- Goal: Fix Store validation errors for v1.0.2 submission caused by incorrect Publisher value
- Files:
  - `packaging/msix/build_msix.ps1`: Removed line that overwrote Publisher with placeholder value
  - `packaging/msix/BUILD_RELEASE_GUIDE.md`: Added troubleshooting for Publisher validation errors
- Diff: 1 line removed from script, troubleshooting section added to guide
- Risk: Low (simple fix - stop overwriting correct manifest value)
- Gates: build ✅ package ✅ verify-script ✅ publisher-correct ✅
- Follow-ups: Upload corrected v1.0.2 to Partner Center

### Root Cause Analysis
**Problem:** Store upload validation rejected v1.0.2 with errors:
- Invalid package family name (expected: `Ef-Map.EF-MapOverlayHelper_r3vrm21jghstm`)
- Invalid package publisher name (expected: `CN=9523ACA0-C1D5-4790-88D6-D95FA23F0EF9`, got: `CN=YOUR_NAME_HERE`)

**Investigation:**
- Base `AppxManifest.xml` has correct Publisher from Partner Center: `CN=9523ACA0-C1D5-4790-88D6-D95FA23F0EF9`
- `build_msix.ps1` line 58 was overwriting it: `$ManifestContent = $ManifestContent -replace 'Publisher="[^"]*"', "Publisher=`"$PublisherName`""` 
- Default parameter value `$PublisherName = "CN=YOUR_NAME_HERE"` was being used
- Result: Correct Publisher from base manifest was replaced with placeholder

**Why This Happened:**
The script was designed to allow customization but defaulted to a placeholder value. The base manifest already had the correct value from Partner Center, so the replacement was unnecessary and harmful.

### Fix Applied
1. Removed the Publisher replacement line from `build_msix.ps1`
2. Removed the unused `$PublisherName` parameter
3. Added comment: `# DO NOT modify Publisher - it's already set correctly in AppxManifest.xml from Partner Center`

**Before (WRONG):**
```powershell
$ManifestContent = $ManifestContent -replace 'Publisher="[^"]*"', "Publisher=`"$PublisherName`""
```

**After (CORRECT):**
```powershell
# DO NOT modify Publisher - it's already set correctly in AppxManifest.xml from Partner Center
```

### Verification
After rebuild, verification script confirmed:
```
Publisher: CN=9523ACA0-C1D5-4790-88D6-D95FA23F0EF9  ✓
```

### Lessons Learned
1. **Base manifest is authoritative**: The `AppxManifest.xml` file contains the correct Partner Center identity; don't override it
2. **Verification must check Publisher**: The verification script now displays Publisher value to catch this
3. **Default parameters are dangerous**: Placeholder values like `CN=YOUR_NAME_HERE` should trigger errors, not be silently used
4. **Package family name is derived**: The family name hash is generated from Name + Publisher, so wrong Publisher breaks both validations

### Updated Documentation
- `BUILD_RELEASE_GUIDE.md`: Added troubleshooting section for Publisher validation errors
- Version history updated to show v1.0.2 had two attempts (first rejected, second corrected)

## 2025-10-31 – Store Submission v1.0.1 Missing Tray Icon (Critical Packaging Failure)
- Goal: Fix missing tray icon in v1.0.1 Store submission and prevent future packaging failures
- Files:
  - `packaging/msix/build_msix.ps1`: Added icon copy step before Store asset generation
  - `packaging/msix/verify_msix_contents.ps1`: NEW - Mandatory pre-upload verification script
  - `packaging/msix/PRE_UPLOAD_CHECKLIST.md`: NEW - Mandatory checklist document
- Diff: 1 file fixed (+8 lines), 2 new verification files created (~200 lines total)
- Risk: Medium (packaging process was broken, now fixed with mandatory verification)
- Gates: build ✅ package ✅ verify-script ✅ icon-present ✅
- Follow-ups: 
  - Build v1.0.2 with fixed script
  - Run verification script (MANDATORY before upload)
  - Upload to Partner Center (will take another 24h for approval)

### Root Cause Analysis
**Problem:** v1.0.1 package approved by Store and installed by user, but tray icon shows generic Windows folder icon instead of EF-Map branded icon.

**Investigation:** Tray application (`tray_application.cpp` lines 74-96) attempts to load icon in this order:
1. First tries: `Assets\app.ico` (compiled Windows icon file)
2. Falls back to: `Assets\Square44x44Logo.png`
3. Final fallback: `IDI_APPLICATION` (generic Windows icon)

**Confirmation:** 
- Icon file exists at: `src/helper/Assets/app.ico` (424 bytes)
- v1.0.1 package contents (extracted): `app.ico` is **MISSING** from `Assets/` folder
- v1.0.1 package only contains: PNG assets (Square44x44Logo.png, etc.) but not the `.ico` file

**Why This Happened:**
The `build_msix.ps1` script (original version):
1. ✅ Copies helper binaries (ef-overlay-tray.exe, ef-overlay.dll, ef-overlay-injector.exe)
2. ✅ Generates PNG assets from EF-Map logo for Store listing requirements
3. ❌ **NEVER copies the `app.ico` file** that the tray application actually needs at runtime

### Fix Applied
Modified `build_msix.ps1` to add icon copy step (between manifest creation and Store asset generation):
```powershell
# Copy tray icon (.ico) that the helper tries to load at runtime
Write-Host "Copying tray icon..."
$HelperIconPath = Join-Path $RepoRoot "src\helper\Assets\app.ico"
if (Test-Path $HelperIconPath) {
    Copy-Item $HelperIconPath -Destination "$StagingDir\Assets\app.ico"
    Write-Host "  OK app.ico copied for tray icon" -ForegroundColor Green
} else {
    Write-Host "  WARNING: Tray icon not found at $HelperIconPath" -ForegroundColor Yellow
}
```

### Verification System Created
Created two new files to prevent this from happening again:

**1. verify_msix_contents.ps1 (Automated Verification):**
- Auto-detects latest MSIX in `releases/` folder
- Extracts package to temporary directory
- Checks all required files (including `Assets\app.ico`)
- Reports file sizes to detect incorrect file types (e.g., script vs binary)
- Checks for accidentally included PowerShell scripts
- Parses and displays manifest version/identity
- Exit code 0 = PASS (safe to upload), Exit code 1 = FAIL (do NOT upload)

**2. PRE_UPLOAD_CHECKLIST.md (Mandatory Process):**
- Step-by-step checklist that MUST be completed before every Store upload
- Documents the two packaging failures we've had (v1.0.0 script mishap, v1.0.1 icon missing)
- Requires running verification script and confirming specific files exist
- Includes file size checks to catch wrong file type issues early
- Documents correct build sequence

### Incident Timeline
1. **v1.0.0 submission:** PowerShell script packaged instead of helper executable → rejected immediately
2. **v1.0.0 resubmission:** Fixed, included correct executable → approved (~24h wait)
3. **v1.0.1 submission:** User reported missing icon after finding helper bug → packaged with fixed helper executable
4. **v1.0.1 resubmission:** AI assured operator all files were present → approved (~24h wait)
5. **v1.0.1 user installation:** Tray icon shows generic Windows icon (fallback), not EF-Map branded icon
6. **Investigation:** Icon file was NEVER copied by packaging script despite assurances

### Lessons Learned
1. **Never trust AI assurances without verification:** "I checked and everything is there" means nothing without running an actual verification script
2. **Automated verification is mandatory:** Human review of 10+ files is error-prone and wastes operator time
3. **Make verification easy:** Auto-detect latest MSIX, extract, check, report pass/fail
4. **Document failures:** Each packaging failure should add a new check to the verification script
5. **24-hour Store review cost:** Each mistake costs a full business day waiting for Microsoft approval

### Next Submission (v1.0.2)
Sequence:
1. Build Release binaries: `cmake --build . --config Release`
2. Package MSIX: `.\build_msix.ps1 -Version "1.0.2" -BuildConfig "Release"`
3. **MANDATORY:** Run `.\verify_msix_contents.ps1` (must show VERIFICATION PASSED)
4. Review checklist: `PRE_UPLOAD_CHECKLIST.md` (confirm all checkboxes)
5. Upload to Partner Center
6. Wait ~24h for approval
7. Test Store-signed package on clean machine (confirm branded tray icon appears)

## 2025-10-30 – Microsoft Store Certification Issues Resolved
- Goal: Fix certification failures for Microsoft Store submission
- Files:
  - Manifest: `packaging/msix/AppxManifest.xml` (fixed XML comment syntax, updated Partner Center identity)
  - Assets: Deleted placeholder PNGs from repository root (`Square150x150Logo.png`, `Square44x44Logo.png`, `StoreLogo.png`, `Wide310x150Logo.png`)
  - Resubmission checklist: `releases/STORE_RESUBMISSION_CHECKLIST.md`
- Diff: 4 files deleted, 1 manifest corrected, 1 checklist created
- Risk: Low (asset packaging fix + manifest identity update)
- Gates: build ✅ package ✅ extract-verify ✅ manifest-correct ✅
- Follow-ups: Upload corrected MSIX to Partner Center, add 2-line dependency disclosure to Store listing

### Certification Issues (Initial Submission 2025-10-30)

**Issue 1: Default/Placeholder Images**
- **Problem:** Microsoft flagged "default image" showing generic blue square tile icon
- **Root Cause:** Repository root contained 188-627 byte placeholder PNG files that were packaged instead of branded assets
- **Expected Assets:** `src/helper/Assets/*.png` (1.4-43 KB branded EF-Map logo files)
- **Packaged Assets:** Root-level placeholders (created Oct 29 13:51, tiny file sizes)
- **Fix:** Deleted all placeholder PNGs from repository root; build script now uses only `src/helper/Assets/`
- **Verification:** Extracted `EFMapHelper-v1.0.0.msix` confirmed correct asset sizes:
  - `SplashScreen.png`: 43.3 KB
  - `Square150x150Logo.png`: 6.9 KB
  - `Square44x44Logo.png`: 1.4 KB
  - `StoreLogo.png`: 1.6 KB
  - `Wide310x150Logo.png`: 11.9 KB

**Issue 2: Undisclosed Dependencies**
- **Policy Violation:** Microsoft policy 10.2.4.1 requires dependency disclosure within first two lines of Store description
- **Dependency:** Microsoft Visual C++ Redistributable (automatically installed if missing)
- **Fix:** Created 2-line disclosure for Store listing:
  ```
  **Requires:** Microsoft Visual C++ Redistributable (automatically installed if missing) and DirectX 12 (Windows 10 version 2004+).
  **Bring real-time EF-Map data directly into EVE Frontier.** Windows native application that renders live route guidance, mining/combat telemetry, and session tracking as an in-game overlay—no alt-tabbing required.
  ```
- **Rationale:** Satisfies policy requirement while maintaining product pitch seamlessly in second line

### Corrected Package Details

**MSIX Identity (verified via extraction):**
```xml
Name="Ef-Map.EF-MapOverlayHelper"
Publisher="CN=9523ACA0-C1D5-4790-88D6-D95FA23F0EF9"
Version="1.0.0.0"
PublisherDisplayName="Ef-Map"
DisplayName="EF-Map Overlay Helper"
```

**Package:** `EFMapHelper-v1.0.0.msix` (1.26 MB unsigned Release build)
- All branded assets verified (no placeholders)
- Manifest uses correct Partner Center identity values
- Ready for resubmission with dependency disclosure

### Next Steps
1. Upload corrected MSIX to Microsoft Partner Center
2. Update Store listing description with 2-line dependency disclosure
3. Resubmit for certification (1-3 day turnaround expected)

---

## 2025-10-29 – MSIX Packaging Complete with UAC Elevation & WindowsApps Workaround
- Goal: Complete Phase 6 local MSIX packaging with working overlay injection
- Files:
  - Helper: `src/helper/helper_runtime.cpp` (injection with UAC elevation, WindowsApps ACL workaround)
  - Packaging: `src/helper/Package.appxmanifest`, `build_msix.ps1`, `sign_msix.ps1`, `create_test_cert.ps1`
- Diff: ~150 lines added (manifest, scripts, UAC elevation, temp directory workaround)
- Risk: Medium (packaging + elevation + ACL workarounds)
- Gates: build ✅ package ✅ sign ✅ install ✅ inject ✅ overlay-rendering ✅
- Follow-ups: Azure Code Signing setup, web app install buttons, Store submission

### Summary
**MSIX packaging fully functional:** Local sideload installation works end-to-end. Overlay injects successfully into game process via elevated injector launched from temp directory to bypass WindowsApps ACL restrictions. Ready for Azure Code Signing and external testing.

### Critical Issues Solved

**Issue 1: String Encoding Corruption**
- **Problem:** `ShellExecuteExW` received garbage pointers causing Korean/Chinese characters error
- **Root Cause:** `injectorPath.wstring().c_str()` creates temporary string, gets pointer, then destroys string
- **Solution:** Store paths as actual variables before taking c_str():
  ```cpp
  std::wstring injectorPathStr = actualInjectorPath.wstring();
  sei.lpFile = injectorPathStr.c_str();  // Valid pointer
  ```

**Issue 2: WindowsApps ACL Restrictions**
- **Problem:** MSIX apps install to `C:\Program Files\WindowsApps\` which blocks elevated execution
- **Root Cause:** WindowsApps folder has TrustedInstaller-only ACLs preventing `ShellExecuteEx` with `runas`
- **Solution:** Copy injector + DLL to `%TEMP%\ef-overlay-inject\` before elevation:
  ```cpp
  bool needsTempCopy = injectorPath.wstring().find(L"WindowsApps") != std::wstring::npos;
  if (needsTempCopy) {
      tempDir = GetTempPath() / L"ef-overlay-inject";
      copy_file(injectorPath, tempDir / L"ef-overlay-injector.exe");
      copy_file(dllPath, tempDir / L"ef-overlay.dll");
  }
  ```
- **Impact:** UAC prompt works correctly from temp location, injection succeeds

**Issue 3: No UAC Prompt on User's Machine**
- **Finding:** User likely has UAC disabled in Windows settings (common for power users/developers)
- **Behavior:** Injection still works (runs elevated automatically without prompt)
- **Plan:** External tester with default UAC settings will validate prompt appears correctly

### Implementation Details

**MSIX Manifest (Package.appxmanifest):**
- Identity: `EFMapOverlayHelper`, Version `0.1.0.0`
- Publisher: `CN=EF Map Project Test Certificate` (self-signed for local testing)
- Capabilities: `runFullTrust` (escape AppContainer sandbox)
- Protocol handler: `ef-overlay://` for web app integration
- Startup task: Disabled by default (user opt-in)
- Executable: `ef-overlay-tray.exe` (GUI tray app, not console helper)

**Build Scripts:**
- `build_msix.ps1`: Automated packaging (copies binaries, data, assets, manifest → makeappx.exe)
- `sign_msix.ps1`: Sign with test certificate using signtool.exe
- `create_test_cert.ps1`: Generate self-signed cert, export PFX, install to Trusted Root
- `install_cert_to_root.ps1`: Standalone script for external PowerShell cert installation

**Injection Flow:**
1. User: Right-click tray icon → "Start Overlay"
2. Helper: Find `exefile.exe` process (game client)
3. Helper: Detect WindowsApps path → copy injector + DLL to temp
4. Helper: Launch injector with `ShellExecuteEx` + `runas` verb → UAC prompt (if enabled)
5. Injector: Elevated process injects DLL via `CreateRemoteThread` + `LoadLibrary`
6. Overlay: DLL hooks DirectX swap chain, renders ImGui overlay
7. User: Press **F8** in-game to toggle overlay visibility

**Validated Behaviors:**
- ✅ Tray app launches on install (Start menu: "EF Map Overlay Helper")
- ✅ Right-click menu shows "Start Overlay" option
- ✅ Injection succeeds (notification: "Overlay DLL injected successfully")
- ✅ Overlay renders in-game (tested with visited systems, session history, follow mode)
- ✅ F8 key toggles overlay visibility
- ✅ Overlay disappears gracefully when helper exits (~5 second heartbeat timeout)
- ✅ Re-injection works after helper restart without game restart

### Next Steps (Phase 6 Complete → Phase 7)

**Immediate (Before External Testing):**
1. ~~Test local sideload~~ ✅ COMPLETE
2. ~~Verify injection works~~ ✅ COMPLETE
3. ~~Smoke test overlay features~~ ✅ COMPLETE

**Phase 7 - Azure Code Signing & Distribution:**
1. Set up Azure Code Signing subscription ($9.99/month)
2. Create signing profile, generate production certificate
3. Sign MSIX with Azure cert (replaces self-signed test cert)
4. Create `.appinstaller` manifest for auto-updates
5. Upload to Cloudflare R2: `ef-helper-releases` bucket
6. Send signed package to external tester for UAC prompt validation
7. Update web app with install/launch buttons (HelperBridgePanel)
8. Deploy preview branch for end-to-end testing

**Phase 8 - Microsoft Store (Optional):**
- Partner Center account ($19-99 one-time)
- Submit signed MSIX with screenshots/description
- Await 1-3 day review
- Update web app with Store badge

### End User Experience (Production)

**Installation (one-time):**
- User clicks "Install Helper" in web app → downloads `.appinstaller` or redirects to Store
- Windows shows UAC prompt during MSIX install (standard for all apps)
- Helper optionally added to startup (user can enable in tray settings)

**Daily Usage:**
- Tray icon appears on login (if startup enabled)
- User clicks "Start Overlay" in tray menu
- **UAC prompt appears** (if UAC enabled) → user clicks "Yes"
- Overlay appears in-game (press F8 to toggle)

**UAC Prompt Frequency:**
- Current: Every time user clicks "Start Overlay"
- Acceptable for beta/MVP (matches OBS Studio, other game tools)
- Future improvements: Windows service (one-time elevation) or kernel driver (no UAC)

### Lessons Learned

**MSIX Packaging Gotchas:**
1. **Manifest naming:** Must be `AppxManifest.xml` (not `Package.appxmanifest`) for makeappx.exe
2. **Certificate matching:** Publisher DN in manifest MUST match signing cert subject exactly
3. **WindowsApps ACLs:** Cannot execute elevated from install location → temp directory workaround required
4. **Temporary lifetimes:** Always store `wstring` variables before calling `.c_str()` on temporaries
5. **VS Code PowerShell:** Cert operations requiring user input hang in integrated terminal → use external PowerShell with `-Verb RunAs`

**Testing Workflow:**
- Always test from MSIX install location, not development builds
- Use external PowerShell for smoke testing (VS Code terminal breaks helper binding)
- Process name `exefile.exe` is stable across launches (PID changes every session)
- F8 toggle key is critical UX (overlay hidden by default to avoid obstruction)

### Files Changed

**New Files:**
- `src/helper/Package.appxmanifest` (MSIX manifest)
- `src/helper/build_msix.ps1` (automated packaging)
- `src/helper/sign_msix.ps1` (test signing)
- `src/helper/create_test_cert.ps1` (self-signed cert generation)
- `src/helper/install_cert_to_root.ps1` (standalone cert installer)
- `src/helper/Assets/*.png` (placeholder icons - 44x44, 150x150, 310x150, 50x50)

**Modified Files:**
- `src/helper/helper_runtime.cpp`:
  - Lines 713-745: Temp directory copy logic for WindowsApps workaround
  - Lines 771-835: ShellExecuteEx with `runas` verb for UAC elevation
  - Line 6: Added `#include <shellapi.h>` for ShellExecuteExW

**Build Artifacts:**
- `build/packages/ef-overlay-helper_unsigned_Debug.msix` (7 files: tray exe, injector, DLL, data folder, assets)
- `build/packages/ef-overlay-helper_signed_Debug.msix` (signed with test cert)

---

## 2025-10-29 – P-SCAN Feature Complete
- Goal: Finalize P-SCAN feature with prerequisite warnings, authentication text cleanup, and follow mode state fixes
- Files:
  - Overlay: `src/overlay/overlay_renderer.cpp` (prerequisite checks, button disable logic)
- Diff: ~20 lines changed (warning display, follow mode state check, button disable logic)
- Risk: Low (polish + bug fixes)
- Gates: build ✅ smoke ✅ (tested in-game with follow mode toggle)
- Cross-repo: EF-Map-main updated (web app auth warning text)

### Summary
**P-SCAN feature complete and validated:** Players can now scan for network nodes in their current system after deploying portable structures. Both overlay and web app enforce prerequisites (follow mode + authentication) with clear warnings and disabled button states when requirements aren't met.

### Changes Made
**Overlay (overlay_renderer.cpp):**
- Added prerequisite checks at top of P-SCAN tab:
  - Follow mode check: `currentState_->follow_mode_enabled`
  - Authentication check: `currentState_->authenticated`
  - Combined: `canScan = hasFollowMode && isAuthenticated`
- Warning display when prerequisites not met:
  - Follow mode disabled: "Follow mode must be enabled in the web app."
  - Not authenticated: "Not authenticated. Connect your wallet in the web app to use P-SCAN."
  - Warnings use yellow text (`ImVec4(0.96f, 0.62f, 0.04f, 1.0f)`)
- Button state:
  - Grayed out (`orangeButtonDisabled = ImVec4(0.5f, 0.5f, 0.5f, 0.5f)`) when `!canScan`
  - Disabled via `ImGui::BeginDisabled(!canScan)` / `ImGui::EndDisabled()` wrapper
  - Applies to both "no scan data" and "scan data exists" states

**Bug Fix:**
- Initially checked `player_marker.has_value()` for follow mode (stays true even when disabled)
- Fixed to use `follow_mode_enabled` boolean field (accurate state reflection)
- Now properly detects when user disables follow mode in overlay/web app

### Validation Complete ✅
- Follow mode disabled → warning appears, button grayed out ✅
- Not authenticated → warning appears, button grayed out ✅
- Both prerequisites met → no warnings, button clickable in orange ✅
- Scan executes → results appear in both overlay and web app ✅
- Data persists across log-watcher updates ✅
- Prerequisites synchronized between overlay and web app ✅

### Follow-up
Phase 5 now complete with all 7 features shipped (Visited Systems, Session History, Next System in Route, Bookmark Creation, Follow Mode Toggle, Legacy Cleanup, **P-SCAN**). Next: Phase 6 (Packaging & Pre-release Readiness).

---

## 2025-10-29 – UI Polish: Button Border Radius Reduction
- Goal: Reduce button border radius from heavy rounding (6.0f) to subtle corner smoothing (2.0f)
- Files:
  - `src/overlay/overlay_renderer.cpp` (7 button types modified)
- Diff: +10 lines (added rounding to 2 buttons that had none), ~7 value changes
- Risk: Low (pure visual polish, no functional changes)
- Changes:
  - **Copy ID button:** `3.0f` → `2.0f` (line 787)
  - **Disable/Enable Tracking button:** `6.0f` → `2.0f` (line 840)
  - **Stop/Start Follow Mode button:** `6.0f` → `2.0f` (line 899)
  - **Add Bookmark button:** `6.0f` → `2.0f` (line 938)
  - **Mining Reset session button:** Added `2.0f` rounding (lines 1401-1411, previously sharp corners)
  - **Combat Reset Session button:** Added `2.0f` rounding (lines 1833-1869, previously sharp corners)
- User Feedback: "Decrease the rounding slightly on all buttons... just to literally smooth off the corners, basically. Leave tabs as they are, leave text input as it is, purely just the interactable buttons."
- Result: More professional appearance, less "bubbly" look, consistent styling across all buttons
- Gates: build ✅ (overlay DLL rebuilt), smoke ✅ (user tested in-game)
- Follow-up: None (complete)

## 2025-10-29 – CRITICAL FIX: Player Position Immediate Display (30-Second Delay Bug)
- Goal: Fix 30-second delay before helper-connected pill shows system name on page load
- Files:
  - `src/helper/helper_server.cpp` (+23 lines: bidirectional state preservation in `ingestOverlayState()`)
  - `eve-frontier-map/src/utils/helperBridge.ts` (+12 lines: immediate GET /overlay/state on WebSocket connect - partial fix)
- Diff: Helper +23 lines, Web app +12 lines
- Risk: High (core state management, affects all overlay interactions)
- Root Cause (After 3+ Hours of Failed Attempts):
  - **Two sources were overwriting each other instead of merging:**
    - **Log watcher** (source="log-watcher"): Publishes `player_marker` from chat logs (authoritative for player position)
    - **Web app** (source="http"): POSTs route data (authoritative for multi-hop routing)
  - **Bug:** When web app POSTed empty route, it **cleared** log watcher's `player_marker` from stored state
  - **Symptom:** GET /overlay/state returned NO `player_marker` field → helper pill showed "Helper Connected" without system name for 15-30 seconds until next log parse
- Discovery Process:
  - ❌ **Attempt 1:** Added immediate log parsing to `forcePublish()` → Reduced delay to ~15s, still present
  - ❌ **Attempt 2:** Added immediate GET request on WebSocket connect → Initial load worked, but next POST cleared player_marker again
  - ✅ **Breakthrough:** Used Chrome DevTools MCP to inspect actual network traffic → found `/overlay/state` response had NO `player_marker` field
  - ✅ **Solution:** Implemented bidirectional state preservation in helper
- Fix Implementation:
  ```cpp
  // helper_server.cpp lines 390-412 (NEW)
  if (source == "http") {
      // Preserve player_marker from log watcher (authoritative for player position)
      if (latestOverlayStateJson_.contains("player_marker")) {
          overlay::PlayerMarker preservedMarker;
          preservedMarker.system_id = marker["system_id"].get<std::string>();
          preservedMarker.display_name = marker["display_name"].get<std::string>();
          preservedMarker.is_docked = marker["is_docked"].get<bool>();
          enriched.player_marker = preservedMarker;
      }
  }
  
  // Lines 414-463 (EXISTING - complementary logic)
  if (source == "log-watcher") {
      // Preserve route/auth from web app (authoritative for routing)
      if (latestOverlayStateJson_.contains("route")) {
          enriched.route = existingRoute;
      }
  }
  ```
- Verification (Chrome DevTools):
  - **Before:** `{"route":[], "version":4}` (no player_marker)
  - **After:** `{"player_marker":{"display_name":"US3-N2F","is_docked":false,"system_id":"30006368"}, "route":[], "version":4}`
- User Testing:
  - ✅ Helper pill shows system name within 2 seconds of page load
  - ✅ Follow mode centers immediately (no waiting)
  - ✅ Add bookmark available immediately (no waiting)
  - ✅ System name persists across route calculations
- Gates: build ✅, smoke ✅ (Chrome DevTools verification + user testing)
- Critical Lesson: **Use Chrome DevTools MCP proactively for browser-side debugging** - inspecting actual network responses revealed the bug in minutes after hours of failed assumptions
- Follow-up: Document bidirectional preservation pattern in LLM troubleshooting guide

## 2025-10-28 – Visited Systems Tracking Verification
- Goal: Clarify user confusion about 404 errors for `/session/visited-systems?type=active-session`
- Files: None (no code changes, documentation only)
- User Confusion: "I don't understand why you said when I queried about the network error that you hadn't wired it up or something."
- Agent's Initial Mistake: Saw 404 and assumed endpoint wasn't implemented
- Reality Check:
  ```cpp
  // helper_server.cpp line 1060:
  if (!maybeSession.has_value()) {
      res.set_content(make_error("No active session").dump(), application_json);
      res.status = 404; // Expected when no active session exists!
      return;
  }
  ```
- **404 is EXPECTED and HARMLESS** when no active session exists (e.g., on first page load before any jumps)
- User Testing:
  - ✅ Jumped to new system → all-time tracking +1
  - ✅ Session tracking +2 (counts docked → undocked as separate visit)
  - ✅ Feature fully working as designed
- Conclusion: No bug, no changes needed. Feature is complete and operational.
- Follow-up: None

## 2025-10-28 – Bookmark Creation Feature (HOTFIX: System Name Extraction)
- Goal: Fix bookmark system_name not being populated when created from overlay
- Files:
  - `src/helper/helper_runtime.cpp` (+11 lines: extract system_name from server overlay state JSON)
  - `src/helper/helper_server.hpp` (+2 lines: public accessor `getLatestOverlayStateJson()`)
- Diff: +13 lines
- Risk: Low (accessor method + JSON field extraction)
- Root Cause:
  - Overlay event payload only contains `system_id` (not `system_name`)
  - Helper event handler was only extracting fields from event payload
  - **Missing:** Lookup of `system_name` from current overlay state's `player_marker.display_name`
- Fix:
  - Added public method `HelperServer::getLatestOverlayStateJson()` to expose overlay state JSON
  - Helper event handler now calls `server_.getLatestOverlayStateJson()` → extracts `player_marker.display_name`
  - WebSocket message payload now includes both `system_id` and `system_name`
- User Testing:
  - ✅ Bookmark created from overlay with notes → appeared in personal folder
  - ❌ System name was empty (only system_id populated)
  - After fix: System name should now populate correctly (e.g., "US3-N2F")
- Gates: build ✅ (helper rebuilt with system name extraction)
- Follow-up: User to test bookmark creation again and confirm system name appears

## 2025-10-28 – Bookmark Creation Feature (FIXES: UI Colors, Text, Web App Removal, Backend Logic)
- Goal: Fix user-reported issues from initial bookmark feature testing
- Files:
  - `src/overlay/overlay_renderer.cpp` (button text, text input color fix)
  - `src/helper/helper_server.cpp` (WebSocket broadcast implementation)
  - `eve-frontier-map/src/components/HelperBridge/HelperBridgePanel.tsx` (-83 lines: removed bookmark section)
  - `eve-frontier-map/src/utils/helperBridge.ts` (+28 lines: bookmark_add_request handler)
  - `eve-frontier-map/src/App.tsx` (+2 lines: `__efAddBookmark` global function)
- Diff: Overlay +4 lines, Helper +15 lines, Web app -55 lines (net removal)
- Risk: Low (UI polish + backend completion)
- User Feedback:
  - ❌ "We don't need the quick bookmark section in EF Helper [web app]" → Removed bookmark UI from web app (bookmarks ONLY in overlay)
  - ❌ "Change the color of the with text [input] to be the muted orange" → Applied `kTabBase` to `ImGuiCol_FrameBg`
  - ❌ "Change the text in the bookmark button to say add bookmark, not just bookmark" → "Bookmark" → "Add Bookmark"
  - ❌ "It did not add a bookmark to either the personal folder or the tribe folder" → Implemented WebSocket broadcast + userOverlayStore integration
  - ℹ️ "For tribe button... only appear if the user is connected" → Auth state population deferred (not blocking bookmark creation MVP)
- Implementation:
  - **Helper Endpoint (`/bookmarks/create`):**
    - Parses `{system_id, system_name, notes, for_tribe}` from POST body
    - Broadcasts WebSocket message `{"type":"bookmark_add_request", "payload":{...}}` to web app
    - Web app receives via `helperBridge.ts` → calls `window.__efAddBookmark(systemId, systemName, color, note)`
    - App.tsx exposes global function → `userOverlayStore.add(...)` (client-side localStorage)
  - **Personal vs Tribe Routing (Future):**
    - Currently: ALL bookmarks → personal folder (client-side)
    - When auth state implemented: `for_tribe=true` → POST to `/api/bookmarks/create` (server-side tribe folder)
  - **Auth State (Deferred):**
    - `state.authenticated`, `state.tribe_id`, `state.tribe_name` fields exist in schema but helper never populates them
    - "For Tribe" checkbox logic correct but never renders (always `authenticated=false`)
    - Future: Helper polls `/api/player-profile` or WebSocket broadcasts profile changes
- Gates: build ✅ typecheck ✅ deploy ✅ smoke (web app: bookmark section removed ✅, helper connected ✅)
- Follow-ups:
  1. Implement helper auth state population (fetch player profile on connect + 30s poll)
  2. Test bookmark creation end-to-end in-game (overlay button click → personal folder entry appears)
  3. Implement tribe bookmark routing when `for_tribe=true` and user authenticated

## 2025-10-28 – Bookmark Creation Feature (Bidirectional Instant Sync)
- Goal: Add one-click bookmark creation from in-game overlay with optional notes and tribe sharing
- Files:
  - `src/shared/event_channel.hpp` (+1 event type: BookmarkCreateRequested)
  - `src/overlay/overlay_renderer.cpp` (+60 lines: bookmark button, text input, tribe checkbox below follow mode)
  - `src/helper/helper_runtime.cpp` (+60 lines: event handler posting to `/bookmarks/create`)
  - `src/helper/helper_server.cpp` (+50 lines: HTTP POST `/bookmarks/create` endpoint)
  - `eve-frontier-map/src/components/HelperBridge/HelperBridgePanel.tsx` (+65 lines: bookmark UI section)
  - `src/shared/overlay_schema.{hpp,cpp}` (+3 fields: authenticated, tribe_id, tribe_name)
  - `eve-frontier-map/src/utils/helperBridge.ts` (+3 fields to OverlayState interface)
- Diff: Overlay +180 lines, Web app +68 lines, Schema +20 lines
- Risk: Medium (new feature with bidirectional flow + UI conditionals)
- Implementation:
  - **Overlay UI:** "Bookmark" button with text input (25 chars) + "For Tribe" checkbox
    - Only visible when follow mode enabled (requires current system from player marker)
    - Tribe checkbox only shown when `authenticated=true` and `tribe_id` present (excludes CloneBank86)
  - **Event Flow (Overlay → Helper → Web App):**
    1. User clicks overlay button → `BookmarkCreateRequested` event with `{system_id, notes, for_tribe}`
    2. Helper event handler receives event → POSTs to `/bookmarks/create` (async detached thread)
    3. Helper HTTP endpoint validates auth → acknowledges success
    4. Web app handles bookmark creation via client-side user overlay store (not yet implemented)
  - **Event Flow (Web App → Helper):**
    1. User clicks web app bookmark button → POST to helper `/bookmarks/create`
    2. Helper endpoint validates auth → acknowledges success
    3. Web app creates bookmark in user overlay store (same path as overlay)
- UI Behavior:
  - **Bookmark button:** Same styling as follow mode / tracking buttons (6px rounded corners, orange theme)
  - **Text input:** 180px width (~25 chars), placeholder "Optional notes"
  - **For Tribe checkbox:** Shows tribe name when authenticated + in player-created tribe
  - **Auto-clear:** Text input and checkbox reset after successful bookmark creation
- Schema Extension:
  - **OverlayState fields:** `authenticated`, `tribe_id`, `tribe_name` (all optional, backward compatible)
  - Helper populates these fields from player profile / session state (future work)
  - Overlay reads fields to conditionally render tribe checkbox
- Integration Points:
  - System ID: Extracted from `state.player_marker->system_id` (requires follow mode)
  - Color: Default orange (`#ff4c26`) matching overlay theme
  - Folder: Personal folder (all) or tribe folder (when `for_tribe=true`)
  - Notes: Optional 25-char text field
- Future Work:
  - Helper populate `authenticated`, `tribe_id`, `tribe_name` from session state
  - Web app client-side bookmark creation (integrate with user overlay store)
  - Helper forward to EF Map worker `/api/bookmarks/create` for server persistence
- Gates: typecheck ✅ | build (overlay) ✅ | build (web app) ✅ | deploy ✅ (`feature-bookmark-creation.ef-map.pages.dev`) | smoke (pending user test)
- Follow-ups:
  - User to test bookmark creation from both overlay and web app
  - Implement client-side bookmark storage in user overlay store
  - Add helper state population for auth/tribe fields

## 2025-10-28 – Instant Bidirectional Sync: HTTP Endpoints Must Also Broadcast
- Goal: Fix web app → overlay direction (still had 10s delay even after overlay → web app was instant)
- Files:
  - `src/helper/helper_server.cpp` (lines 1107, 1145, 1277: added `updateTrackingFlag()` and `updateSessionState()` calls in HTTP POST handlers)
  - `eve-frontier-map/src/utils/helperBridge.ts` (added tracking/session fields to OverlayState interface)
  - `eve-frontier-map/src/components/HelperBridge/HelperBridgePanel.tsx` (added WebSocket listener for instant UI updates)
- Diff: Helper +9 lines (3 broadcast calls), Web app +50 lines (interface + effect)
- Risk: Low (reusing proven direct update pattern)
- Root cause:
  - **Overlay → Web App:** Working instantly after adding WebSocket listener ✅
  - **Web App → Overlay:** Still had 10s delay ❌
  - Web app POSTs to `/session/visited-systems/toggle`, `/session/start-session`, `/session/stop-session`
  - HTTP handlers updated `sessionTracker_` state ✅
  - **BUT** handlers didn't call `updateTrackingFlag()` / `updateSessionState()` ❌
  - Overlay only saw changes when heartbeat or log watcher updated shared memory (~2-30s delay)
- User observation:
  - After WebSocket fix: overlay buttons → web app < 1 second ✅
  - Web app buttons → overlay: still ~10 seconds ❌
  - Follow mode: instant both directions (HTTP POST + response pattern) ✅
- Implementation:
  ```cpp
  // BEFORE (HTTP endpoints):
  tracker->setAllTimeTrackingEnabled(enabled);
  nlohmann::json payload{{"status", "ok"}, {"enabled", enabled}};
  res.set_content(payload.dump(), application_json);
  // ❌ No broadcast - overlay waits for next heartbeat/log update
  
  // AFTER:
  tracker->setAllTimeTrackingEnabled(enabled);
  updateTrackingFlag(enabled);  // ✅ Instant shared memory + WebSocket broadcast
  nlohmann::json payload{{"status", "ok"}, {"enabled", enabled}};
  res.set_content(payload.dump(), application_json);
  ```
- Complete flow (both directions now):
  
  **Overlay → Web App:**
  1. User clicks overlay button
  2. Overlay posts event to helper via event channel
  3. Helper event handler calls `updateTrackingFlag()` / `updateSessionState()`
  4. Direct JSON update + shared memory write + WebSocket broadcast
  5. Web app receives WebSocket message + updates UI instantly (< 1s) ✅
  
  **Web App → Overlay:**
  1. User clicks web app button
  2. Web app POSTs to helper HTTP endpoint
  3. Helper HTTP handler calls `updateTrackingFlag()` / `updateSessionState()`
  4. Direct JSON update + shared memory write + WebSocket broadcast
  5. Overlay reads from shared memory + updates UI instantly (< 1s) ✅
  6. Web app also receives WebSocket broadcast for confirmation ✅
- Gates:
  - ✅ Helper rebuilt with HTTP endpoint broadcasts
  - ✅ Helper restarted and overlay injected
  - ✅ Web app deployed with WebSocket listener
  - ⏳ User confirmation: instant bidirectional sync for tracking AND session buttons
- Expected behavior:
  - **All-Time Tracking toggle:** Web app ↔ overlay instant (< 1 second) both directions
  - **Session Start/Stop:** Web app ↔ overlay instant (< 1 second) both directions
  - **Follow Mode:** Still instant (unchanged)
- Follow-ups:
  - User confirms all buttons work instantly in both directions
  - Bookmark feature will use same patterns (overlay event + HTTP POST, both calling direct update methods)

## 2025-10-28 – Instant State Broadcast Fix: Direct Update Methods (Not forcePublish)
- Goal: Eliminate 5-15 second delay when overlay buttons toggle tracking/session state - make updates instant like follow mode
- Files:
  - `src/helper/helper_server.hpp` (added `updateTrackingFlag()` and `updateSessionState()` method declarations)
  - `src/helper/helper_server.cpp` (lines 129-228: implemented direct state update methods mirroring `updateFollowModeFlag()`)
  - `src/helper/helper_runtime.cpp` (lines 797-850: replaced `forcePublish()` calls with direct server update calls)
- Diff: +105 lines (2 new methods + updated event handlers)
- Risk: Medium (changes core state broadcast mechanism, but mirrors proven follow mode pattern)
- Root cause (CORRECTED after user testing):
  - **Initial wrong approach:** Called `logWatcher_->forcePublish()` which goes through log watcher path
  - **Actual problem:** Follow mode uses DIRECT state update (`updateFollowModeFlag()`) that:
    1. Updates JSON directly: `latestOverlayStateJson_["follow_mode_enabled"] = enabled`
    2. Writes to shared memory immediately: `sharedMemoryWriter_.write(serialized, ...)`
    3. Broadcasts via WebSocket immediately: `websocketHub_->broadcastOverlayState(envelope)`
  - **forcePublish() path is slow because:**
    - Goes through log watcher → builds state from scratch → enriches → broadcasts
    - Multiple indirection layers + mutex locks
    - Designed for position/telemetry updates, not instant UI sync
- User observation after first fix attempt:
  - Overlay buttons still update locally (shared memory read works)
  - Web app still has 5-15 second delay receiving changes
  - Follow mode STILL works instantly
  - User correctly noted: "you would be clever enough to look at how follow mode works and just duplicate it"
- Implementation (correct approach):
  ```cpp
  // NEW: Direct update methods mirroring follow mode pattern
  bool HelperServer::updateTrackingFlag(bool enabled)
  {
      std::lock_guard<std::mutex> guard(overlayStateMutex_);
      latestOverlayStateJson_["visited_systems_tracking_enabled"] = enabled;
      latestOverlayStateJson_["heartbeat_ms"] = now_ms();
      // ... serialize + write to shared memory + broadcast WebSocket
      sharedMemoryWriter_.write(serialized, version, generatedAt);
      websocketHub_->broadcastOverlayState(envelope);
  }
  
  bool HelperServer::updateSessionState(bool hasActive, std::optional<std::string> id)
  {
      std::lock_guard<std::mutex> guard(overlayStateMutex_);
      latestOverlayStateJson_["has_active_session"] = hasActive;
      latestOverlayStateJson_["active_session_id"] = id.has_value() ? *id : nullptr;
      // ... same immediate broadcast pattern
  }
  ```
- Event handler changes:
  ```cpp
  // BEFORE (wrong):
  sessionTracker_->setAllTimeTrackingEnabled(!currentState);
  if (logWatcher_) {
      logWatcher_->forcePublish();  // ❌ Slow indirect path
  }
  
  // AFTER (correct):
  sessionTracker_->setAllTimeTrackingEnabled(!currentState);
  if (!server_.updateTrackingFlag(!currentState)) {  // ✅ Direct like follow mode
      spdlog::debug("Tracking flag update deferred; overlay state not yet available");
  }
  ```
- Why this works (same as follow mode):
  1. **Direct JSON update** → no rebuilding from log watcher snapshot
  2. **Immediate shared memory write** → overlay sees change instantly
  3. **Immediate WebSocket broadcast** → web app receives in < 200ms
  4. **Single mutex lock** → minimal latency
- Gates:
  - ✅ C++ compilation successful (both helper_server.cpp and helper_runtime.cpp updated)
  - ✅ Helper rebuilt and restarted with direct update methods
  - ✅ Overlay DLL injected
  - ⏳ User confirmation: tracking/session buttons now update web app instantly (< 1 second) like follow mode
- Expected behavior:
  - Click **All-Time Tracking** → overlay + web app update instantly (both directions)
  - Click **Start Session** → overlay + web app show active session instantly (both directions)
  - Click **Stop Session** → overlay + web app show "None" instantly (both directions)
  - All three match follow mode's instant bidirectional sync
- Lessons learned:
  - Don't assume `forcePublish()` is fast enough for UI sync
  - Always check HOW the working feature (follow mode) actually implements instant updates
  - Direct state manipulation >> indirect publish paths for instant feedback
- Follow-ups:
  - User confirms instant updates for all three button types
  - Bookmark button will use same direct update pattern

## 2025-10-28 – Route Serialization Fix: Always Include All Fields (Not Just When > 0)
- Goal: Fix helper serialization to always include planet_count, network_nodes, route_position, total_route_hops in JSON output (prevents missing fields when values are 0)
- Files:
  - `src/shared/overlay_schema.cpp` (lines 505-527: removed conditional serialization, always include all 8 fields)
- Diff: -17 lines (removed conditional logic), +4 lines (direct field assignment)
- Risk: Low (all fields already in schema, overlay renderer handles 0 values correctly)
- Root cause:
  - `serialize_overlay_state()` only included optional fields when value > 0
  - Destination systems often have `network_nodes=0`, so that field was omitted from GET `/overlay/state` response
  - Overlay renderer defaults missing fields to 0, correctly hides them from display
  - User saw correct display for intermediate hops but destination showed only minimal info
- User observation:
  - 2-hop route (origin → destination): "Destination: I8N-FS6" with no hop/planet/node info
  - 3-hop route (origin → intermediate → destination): Intermediate showed full info, destination showed minimal
- Timing:
  - Initial POST from web app includes all fields (even when 0)
  - Helper deserializes correctly
  - BUT when helper serializes for GET endpoint, it skipped 0-value fields
  - Overlay renderer fetches state, sees missing fields, uses default 0, correctly hides them
- Fix logic:
  - Before: `if (node.planet_count > 0) { json["planet_count"] = ... }`
  - After: Always include in initial JSON object: `{"planet_count", node.planet_count}, ...`
  - All 8 fields now serialized unconditionally: system_id, display_name, distance_ly, via_gate, planet_count, network_nodes, route_position, total_route_hops
- Gates:
  - ✅ C++ compilation successful
  - ✅ Helper rebuilt (stopped running instance first)
  - ✅ Automated smoke test: GET `/overlay/state` now shows `"network_nodes":0` for destination hop
  - ⏳ User visual confirmation: destination should show "hop 3/3 | 2 planets" (network nodes hidden because = 0)
- Follow-ups:
  - User confirms destination display includes hop position and planet count
  - Verify route details persist after 30+ seconds (through log watcher updates)

## 2025-10-28 – Route Preservation Fix: Missing Planet/Node/Position Fields
- Goal: Fix helper route preservation logic to include planet_count, network_nodes, route_position, total_route_hops when deserializing from existing JSON (prevents 10-20s disappearance of route details in overlay)
- Files:
  - `src/helper/helper_server.cpp` (lines 271-279: added 4 missing field deserializations)
- Diff: +4 lines (field assignments)
- Risk: Low (filling in missing fields that were already in the schema)
- Root cause:
  - Log watcher sends position updates every ~30 seconds
  - Helper preserves route from web app but only deserialized 4 fields (system_id, display_name, distance_ly, via_gate)
  - Missing fields (planet_count, network_nodes, route_position, total_route_hops) defaulted to 0 after first log watcher update
  - Overlay correctly hides fields when they're 0, so display reverted to minimal format 10-20s after route calculation
- User observation:
  - Initial route display correct: "E5J-F55 (jump) - 53.64 ly | hop 2/4 | 1 planet"
  - After 10-20 seconds: "E5J-F55 (jump) - 53.64 ly" (hop/planet info disappeared)
  - Consistent with 30-second log watcher publish interval
- Implementation:
  - Added deserialization for planet_count, network_nodes, route_position, total_route_hops from latestOverlayStateJson_
  - Mirrors existing pattern for system_id/display_name/distance_ly/via_gate
  - Preserves ALL route fields when log watcher sends position updates
- Gates:
  - ✅ C++ compilation successful
  - ✅ Helper rebuilt (stopped running instance first)
  - ⏳ User smoke test: Verify overlay display persists hop/planet info after 30+ seconds
- Follow-ups:
  - ⏳ User will restart helper and verify route details persist
  - ⏳ Add network_nodes population in web app route transformation (currently missing from web app payload)

## 2025-10-28 – Automated Smoke Testing Workflow: Assistant Executes All Scriptable Steps
- Goal: Clarify that assistant must execute helper injection, route calculation (Chrome DevTools), and status verification automatically - user only launches helper externally and provides visual confirmation
- Files:
  - `AGENTS.md` (added "Automated Smoke Testing Workflow" section replacing brief smoke testing note)
  - `.github/copilot-instructions.md` (added "Automated Smoke Testing Protocol" section with explicit DO NOT ASK USER list)
- Diff: +21 lines AGENTS.md, +16 lines copilot-instructions.md
- Risk: Low (documentation only, workflow optimization)
- Root cause:
  - Workflow optimization was implicit in practice but not documented in guardrails
  - Assistant repeatedly asked user to perform steps (injector, browser route calc, API checks) that can be executed 10-100x faster via tools
  - User's role should be limited to: external PowerShell helper launch, visual in-game confirmation, strategic direction
- Implementation:
  - Added dedicated "Automated Smoke Testing Workflow" section to AGENTS.md with explicit responsibilities split
  - User responsibilities: Launch helper externally, provide visual confirmation, give instructions
  - Assistant responsibilities: Injection via `run_in_terminal`, route calculation via Chrome DevTools MCP, status verification via PowerShell commands
  - Added explicit DO NOT ASK USER list: running injector manually, opening browser, checking API endpoints
  - Rationale statement: "LLM can execute these steps 10-100x faster than manual user execution"
  - Reinforced external PowerShell requirement (helper binding fails in VS Code integrated terminals)
  - Process name requirement: `exefile.exe` (not PID)
- Gates:
  - ✅ Documentation updated in both AGENTS.md and copilot-instructions.md
  - ✅ Workflow split clearly defined with explicit boundaries
  - ✅ Rationale provided for why assistant should execute instead of delegating
- Follow-ups:
  - ⏳ Assistant will execute smoke test steps immediately following this update
  - ⏳ User only needs to confirm helper running externally and report in-game overlay results
- Workflow benefits:
  - 10-100x faster iteration cycles (seconds vs minutes per test)
  - User focuses on strategic direction and visual confirmation instead of scripting
  - Assistant leverages available tools (run_in_terminal, Chrome DevTools MCP, PowerShell commands)
  - Fundamental optimization that makes overlay development efficient

## 2025-10-28 – Critical Overlay Testing Requirements: External PowerShell + Process Name
- Goal: Reinforce smoke testing requirements in copilot-instructions after assistant violated external PowerShell rule, document injector `.exe` extension requirement
- Files:
  - `c:\EF-Map-main\.github\copilot-instructions.md` (added CRITICAL smoke testing rule at top)
  - `c:\ef-map-overlay\.github\copilot-instructions.md` (added CRITICAL smoke testing rule at top)
- Diff: +2 lines per file (new paragraph with bold CRITICAL heading)
- Risk: Low (documentation only, no code changes)
- Root cause:
  - Assistant ran helper in VS Code integrated terminal despite requirement being documented in AGENTS.md and LLM_TROUBLESHOOTING_GUIDE.md
  - Helper started successfully but overlay showed "waiting on overlay state" → helper wasn't communicating with overlay
  - Requirement was buried in Quality Gates section instead of being prominent at file start
- Injector process name discovery:
  - Injector requires `.exe` extension: `exefile.exe` NOT `exefile`
  - `find_process_by_name()` does exact match on `entry.szExeFile` (includes extension)
  - Error was `[error] Unable to resolve target process: exefile`
  - Success: `[info] Injection completed (PID=31444)` with `exefile.exe`
- Implementation:
  - Added **CRITICAL Overlay Smoke Testing Rule** paragraph immediately after repository purpose statement in both copilot-instructions
  - Rule states: helper MUST launch from external PowerShell, VS Code terminals fail to bind correctly, use `exefile.exe` (not PID)
  - Cross-referenced existing documentation (AGENTS.md, LLM_TROUBLESHOOTING_GUIDE.md, decision logs)
- Gates:
  - ✅ Documentation updated in both repos
  - ✅ Rule positioned at top of copilot-instructions for maximum visibility
  - ✅ User confirmed helper binding issue caused by VS Code terminal
- Follow-ups:
  - ⏳ User will launch helper externally and retry injection with `exefile.exe`
  - ⏳ Assistant must ALWAYS check for external PowerShell requirement before running helper during smoke tests
- Rationale: This is non-negotiable for overlay functionality; integrated terminal binding failure is consistent and documented; making rule prominent prevents future violations

## 2025-10-28 – Overlay Route Display: Format & Clipboard Fixes
- Goal: Fix spurious "?" character, add missing planet/node counts, and implement EVE Online link format for clipboard
- Files:
  - `src/overlay/overlay_renderer.cpp` (lines 1-10: added includes, lines 755-792: consolidated display format, lines 802-820: EVE link clipboard)
- Diff: +3 lines (includes), +38 lines (display rebuild), +8 lines (clipboard format change)
- Risk: Low (UI polish, established pattern for clipboard operations)
- Root cause:
  - Em dash character `—` in format string not supported by overlay font, rendered as `?`
  - Planet count and network nodes displayed via separate `ImGui::SameLine()` calls, but data WAS populated correctly
  - Clipboard only copied raw system ID instead of EVE Online link format
- Implementation:
  - Replaced `ImGui::Text()` with multiple `SameLine()` calls → single `std::ostringstream` building complete string
  - Changed em dash `—` to hyphen `-` for compatibility
  - Consolidated all route info on single line: `E5J-F55 (jump) - 59.37 ly | hop 2/4 | 1 planet | 0 nodes`
  - Copy ID now builds EVE link: `<a href="showinfo:5//SYSTEM_ID">SYSTEM_NAME</a>` (matches web app context menu behavior)
  - Added `<sstream>` and `<iomanip>` includes for stringstream and `std::setprecision`
- Gates:
  - ✅ C++ compilation: Successful build after format changes
  - ✅ Overlay DLL rebuild: ef-overlay.dll updated successfully
  - ⏳ User smoke test: Verify display format, planet/node counts, and EVE link clipboard paste in-game
- User benefits:
  - Clean single-line display with all route details visible
  - Copy ID now produces EVE link format that can be pasted directly into in-game chat/notes
  - No more spurious "?" character breaking visual clarity
- Follow-ups:
  - ⏳ User verification: Test in-game paste of copied EVE link
  - ⏳ Feature 3 status: Mark complete in GAME_OVERLAY_PLAN.md after user confirms working

## 2025-10-28 – Overlay Route Navigation: Log Watcher State Merging Fix
- Goal: Prevent log watcher from overwriting route data sent from web app when follow mode updates player position
- Files:
  - `src/helper/helper_server.cpp` (lines 257-287: route preservation logic in `ingestOverlayState`)
- Diff: +31 lines (route/active_route_node_id preservation when source="log-watcher")
- Risk: Medium (core overlay state management, affects route display persistence)
- Root cause:
  - When log watcher detects player movement (follow mode), it calls `ingestOverlayState(state, payloadBytes, "log-watcher")` with a **new** OverlayState containing only `player_marker` (current position)
  - This **replaced** `latestOverlayStateJson_` including the route data sent from web app
  - Result: overlay initially showed correct next hop, then after ~20-30 seconds (log watcher update) reverted to wrong system
  - Confirmed via Chrome DevTools MCP: web app sends route once with correct `active_route_node_id`, NO re-sends after follow mode updates ✅
- Implementation:
  - Added conditional logic: when `source == "log-watcher"` AND existing state contains multi-hop route (size > 1), **preserve** route data from web app
  - Manually deserializes existing route nodes and `active_route_node_id` from `latestOverlayStateJson_` and merges into new state
  - Only `player_marker` and `telemetry` data updated from log watcher; route remains authoritative from web app
  - Fix ensures web app's `active_route_node_id` (calculated as "player at hop N, show hop N+1") persists across follow mode location updates
- Gates:
  - ✅ C++ compilation: Successful rebuild after manual JSON deserialization logic
  - ✅ Helper rebuild: ef-overlay-helper.exe rebuilt successfully
  - ✅ Chrome DevTools MCP testing:
    - Route calculated US3-N2F → EB8-911 (4 hops)
    - Web app sends route with `active_route_node_id = "30008925"` (E5J-F55) ✅
    - Initial From field: E5J-F55 (routing logic selected it) ✅
    - After 30+ seconds: From field reverted to US3-N2F (follow mode update) ✅
    - Route Systems window: still showing all 4 hops correctly ✅
    - Helper console: "Overlay state accepted via log-watcher (436 bytes)" every ~30s ✅
- Technical details:
  - Log watcher `buildOverlayState()` (log_watcher.cpp line 1419) creates new state with single-item route (current position only)
  - Heartbeat thread (helper_server.cpp line 337) runs every 2 seconds but only updates `heartbeat_ms` field, does NOT modify route
  - Log watcher updates trigger every ~20-30 seconds when player position detected in game logs (Local chat entries)
- User benefits:
  - Overlay now shows consistent next hop even when follow mode updates player position
  - No more "reversion to wrong system after 30 seconds" bug
  - Web app remains authoritative source for routing logic; helper only tracks player position
- Follow-ups:
  - ⏳ User smoke test in actual game with overlay injected (verify overlay HUD shows E5J-F55 consistently)
  - ⏳ Rebuild overlay DLL to ensure displayIndex fix + helper route preservation both deployed
  - ⏳ Update GAME_OVERLAY_PLAN.md with technical details of route state management
- Cross-repo: Related fix in overlay_renderer.cpp (removed double-increment bug) documented in separate decision log entry

## 2025-10-27 – Phase 5 Feature 2: Session History with Dropdown
- Goal: Allow users to browse and visualize past stopped sessions on the map
- Files:
  - Backend: `src/helper/session_tracker.hpp` (+1 method `listStoppedSessions()`), `src/helper/session_tracker.cpp` (+92 lines implementation), `src/helper/helper_server.cpp` (+48 lines `/session/list-sessions` endpoint)
  - Frontend (EF-Map-main): `eve-frontier-map/src/components/HelperBridge/HelperBridgePanel.tsx` (+3 state variables, +25 lines fetch logic, +56 lines dropdown UI), `eve-frontier-map/src/App.tsx` (type update + fetch logic for past sessions)
- Diff: Backend +141 lines, Frontend +84 lines
- Risk: Low (extends existing session tracking, follows established patterns)
- Implementation:
  - **Backend `listStoppedSessions()` method:** Scans data directory for `session_*.json` files (excluding `active_session.json`), parses metadata (session_id, start/end times, systems count), returns sorted by newest first. Efficiently loads only metadata (not full system data) for performance.
  - **HTTP endpoint:** GET `/session/list-sessions` returns JSON array of session summaries `[{session_id, start_time_ms, end_time_ms, system_count}, ...]`. Requires auth.
  - **Frontend dataset type:** Extended `VisitedSystemsDataset` type to support ``past-session:${session_id}`` format (template literal type). Radio button + dropdown UI pattern.
  - **Dropdown UX:** Third radio option "Past session" with conditional dropdown showing formatted session dates + system counts (e.g., "27 Oct 2025, 5:06 pm (2 systems)"). Dropdown only appears when Past session radio is selected. Auto-selects most recent session on first click.
  - **Map visualization:** Reuses existing `/session/visited-systems?type=session&session_id=<id>` endpoint. Stars colored for selected past session systems.
  - **Polling:** `fetchPastSessions()` called alongside `fetchVisitedData()` every 30 seconds when Overview tab active.
- Gates:
  - ✅ C++ compilation: No errors after adding `listStoppedSessions()` and endpoint
  - ✅ Helper build: Successful rebuild with new functionality
  - ✅ TypeScript: No errors after adding past-session support
  - ✅ Frontend build: Vite build successful
  - ✅ Deployment: Preview at https://feature-visited-systems-hist.ef-map.pages.dev
  - ✅ Chrome DevTools testing:
    - Past session radio button visible ✓
    - Dropdown appears with 14 sessions ✓
    - Sessions formatted correctly with dates and counts ✓
    - Most recent session auto-selected ✓
- User benefits:
  - Browse historical gameplay sessions without needing active session
  - Visualize where you went during specific past play sessions
  - Compare different sessions by switching between them in dropdown
  - All sessions persist across helper restarts (stored in `%LOCALAPPDATA%\EFOverlay\data\`)
- Follow-ups:
  - ⏳ User testing: verify map stars appear when past session selected
  - ⏳ Phase 5 Feature 3: Export session data (JSON/CSV)
  - ⏳ Phase 5 Feature 4: Date range filters
  - ⏳ Phase 5 Feature 5: Advanced visualization (heatmap, visit counts)
- Cross-repo: EF-Map-main decision log updated with matching entry for frontend changes

## 2025-10-27 – Phase 5 Feature 1 Complete: Reset Session UI + CORS fix
- Goal: Add Reset Session button matching all-time tracking UX, fix CORS blocking visited systems endpoints
- Files:
  - Backend: `src/helper/session_tracker.hpp` (+1 method declaration), `src/helper/session_tracker.cpp` (+14 lines `resetActiveSession()` implementation), `src/helper/helper_server.cpp` (+28 lines `/session/reset-session` endpoint, +1 CORS header fix)
  - Frontend (EF-Map-main): `eve-frontier-map/src/components/HelperBridge/HelperBridgePanel.tsx` (+1 state variable, +~90 lines Reset Session button + themed confirmation dialog)
- Diff: Backend +43 lines, Frontend +91 lines
- Risk: Low (follows established patterns for session management + all-time reset UX)
- Implementation:
  - **Backend `resetActiveSession()` method:** Clears `activeSession_->systems` map while keeping session active (vs `stopSession()` which finalizes + saves to timestamped file). Logs action and calls `saveActiveSession()`.
  - **HTTP endpoint:** POST `/session/reset-session` with auth requirement, returns 404 if no active session, 200 OK on success.
  - **CORS fix:** Added `X-EF-Helper-Auth` to `Access-Control-Allow-Headers` in helper_server.cpp (was blocking preflight OPTIONS requests from deployed web app).
  - **Frontend UX:** Two-button layout (Stop Session | Reset Session) matching all-time tracking pattern (Disable Tracking | Reset All-Time). Reset Session button **always visible** (not conditional on active session per user request: "I don't see any reason why we shouldn't be able to reset the session when the session isn't active because you would do that prior to starting a new session").
  - **Themed confirmation dialog:** Shows current system count, "cannot be undone" warning, styled with accent color matching Reset All-Time dialog.
- Gates:
  - ✅ TypeScript compilation: No errors after adding confirmResetSession state + button layout
  - ✅ Build: Frontend built successfully, helper rebuilt with new endpoint
  - ✅ Deployment: Preview deployed to https://feature-visited-systems-rese.ef-map.pages.dev
  - ✅ Chrome DevTools MCP testing:
    - Reset Session button visible without active session ✓
    - Reset Session button visible with active session ✓
    - Themed confirmation dialog appears on click ✓
    - Confirm Reset sends POST → 200 OK `{"message":"Active session reset","status":"ok"}` ✓
    - Session remains active after reset (timestamp persists, systems cleared) ✓
  - ✅ User smoke test: All buttons working, session start/stop/reset verified functional
- User observations:
  - All-time tracking showing fewer systems after helper rebuild: Expected behavior - fresh `visited_systems.json` on new build. Old data persists in previous helper working directory, can be merged if needed.
  - Stop session creates new session on restart: Correct behavior - each session is discrete with unique session_id. Stopped sessions saved to timestamped JSON files.
  - Session persistence: All-time data, active session, and stopped sessions all persist across helper restarts ✓
- Follow-ups:
  - ⏳ Verify star visualization colors on map canvas (screenshot saved, not yet confirmed by user)
  - ⏳ Phase 5 remaining features (2-5): session history panel, export, filters, advanced visualization
- Cross-repo: EF-Map-main decision log updated with matching entry for frontend changes

## 2025-10-27 – Bug fix: Session start deadlock caused by non-recursive mutex
- Goal: Resolve HTTP 404 error on `/session/start-session` endpoint while `/session/stop-session` worked correctly.
- Files:
  - `src/helper/session_tracker.hpp` (line 71: `std::mutex sessionMutex_` → `std::recursive_mutex sessionMutex_`)
  - `src/helper/session_tracker.cpp` (lines 106, 165, 216, 222, 233, 292, 336: all `std::lock_guard<std::mutex>` → `std::lock_guard<std::recursive_mutex>`)
  - `src/helper/helper_server.cpp` (temporary debug logging added during investigation, removed after fix)
- Diff: +8 lines (mutex type change + 7 lock_guard template updates), -40 lines (debug logging cleanup)
- Risk: Medium (core session tracking, but fix is minimal and well-tested)
- Root cause:
  - `SessionTracker::startSession()` locked `sessionMutex_`, then internally called another method that attempted to lock the same mutex
  - Non-recursive `std::mutex` deadlocks when same thread tries to lock twice → threw exception "resource deadlock would occur"
  - cpp-httplib's exception handler caught the exception but returned HTTP 404 instead of expected 500 status (likely due to early return path)
- Investigation process:
  - Checked for stale binary (rebuild confirmed)
  - Verified route registration (identical to working stop-session)
  - Tested for invisible characters in route strings (none found)
  - Added extensive debug logging → handler WAS executing successfully
  - Simplified handler to "HELLO WORLD" → HTTP 200 (worked!)
  - Re-added authorization → HTTP 200 (worked!)
  - Re-added full business logic → HTTP 404 (failed!)
  - Added try-catch around `tracker->startSession()` → revealed deadlock exception
- Fix: Changed mutex type from `std::mutex` to `std::recursive_mutex` to allow same thread to re-lock without deadlock (critical for methods calling other methods that lock)
- Gates:
  - ✅ Typecheck: No compilation errors after lock_guard template updates
  - ✅ Build: Clean build (Visual Studio 2022, Debug config)
  - ✅ Smoke: Both `/session/start-session` and `/session/stop-session` return HTTP 200 with valid JSON payloads
  - ✅ Test results:
    - POST `/session/start-session` → `{"session_id":"session_20251027_141856","status":"ok"}`
    - POST `/session/stop-session` → `{"message":"Session stopped","status":"ok"}`
- Follow-ups:
  - ✅ Remove temporary debug logging (completed)
  - ⏳ End-to-end test: Web app → start session → visit systems → verify session file persistence
  - ⏳ Implement orange star visualization on EF Map for visited systems
- Cross-repo: Updated `GAME_OVERLAY_PLAN.md` in both repos (overlay + EF-Map-main) marking start-session bug as resolved

## 2025-10-24 – Phase 5 Feature 1 Web App Integration: Helper panel redesign & visualization approach
- Goal: Clarify EF-Map web app integration design decisions for visited systems tracking and broader helper panel UX.
- Files: `docs/initiatives/GAME_OVERLAY_PLAN.md` (this repo + EF-Map-main mirror)
- Design decisions:
  - **Helper panel tab structure:** Reorganize into four tabs similar to Routing window:
    1. **Overview:** Visited systems toggle status, active session indicator, helper connection status (NO route duplication - Route Systems window already exists)
    2. **Mining:** Placeholder for Phase 2 mining telemetry data
    3. **Combat:** Placeholder for Phase 3 combat telemetry data
    4. **Connection/Installation:** Current helper panel content moves here (status, retry button, logs, installation guide)
  - **Map visualization:** Orange star coloring for visited systems (consistent with existing region highlights + jump bubble range). NO halos or hover counts initially. Visit counts tracked in backend but not displayed in first iteration.
  - **Future enhancement (parked):** Potentially reuse Route Systems window logic to surface visited systems list with counts.
  - **Helper detection:** Automatic on Connection/Installation tab with existing retry button for manual reconnection.
- Rationale:
  - Avoid UI duplication: Route Systems window already shows route waypoints → no need to duplicate Next System display in web app (overlay-only feature).
  - Visual consistency: Orange coloring matches existing map highlight patterns (regions, jump range) for familiar UX.
  - Tab organization: Mirrors Routing window structure users already understand; keeps related telemetry grouped (Overview = general, Mining/Combat = specific domains).
  - Phased approach: Track visit counts now, decide display mechanism later (list window vs tooltips vs sparklines) based on user feedback.
- Cross-repo: Mirrored in `EF-Map-main/docs/initiatives/GAME_OVERLAY_PLAN.md` (both updated simultaneously).
- Follow-ups:
  - ⏳ Create feature branch `feature/overlay-visited-systems` on EF-Map-main
  - ⏳ Implement Helper panel tab structure (Overview/Mining/Combat/Connection)
  - ⏳ Add orange star coloring for visited systems in map renderer
  - ⏳ Deploy to Pages preview for validation

## 2025-10-23 – Phase 5 Feature 1 complete: Visited Systems Tracking integration
- Goal: Implement full visited systems tracking with JSON persistence, automatic system jump detection, and HTTP API endpoints.
- Files:
  - Persistence: `src/helper/session_tracker.hpp`, `src/helper/session_tracker.cpp` (NEW - 491 lines, full JSON I/O for all-time + session tracking)
  - Integration: `src/helper/helper_runtime.hpp`, `src/helper/helper_runtime.cpp` (SessionTracker member, initialization, log watcher callback wiring)
  - API: `src/helper/helper_server.hpp`, `src/helper/helper_server.cpp` (5 HTTP endpoints, SessionTrackerProvider pattern)
  - Build: `src/helper/CMakeLists.txt` (added session_tracker.cpp to build)
- Features implemented:
  - **AllTimeVisitedSystems:** Toggle-based persistent tracking across all sessions (JSON file: `visited_systems.json`)
  - **SessionVisitedSystems:** Timestamped play session tracking with session ID `session_YYYYMMDD_HHMMSS` (separate JSON per session)
  - **Automatic tracking:** Log watcher callback records system visits when player jumps (uses PlayerMarker.system_id + display_name from OverlayState)
  - **Thread-safe operations:** All-time and session data protected by separate std::mutex instances
  - **Data directory:** `%LocalAppData%\EFOverlay\data\` (fallback to temp if unavailable)
  - **HTTP API endpoints:**
    1. `GET /session/visited-systems?type=all` → all-time data (version, tracking_enabled, last_updated_ms, systems map)
    2. `GET /session/visited-systems?type=session&session_id=X` → specific session data
    3. `GET /session/visited-systems?type=active-session` → current active session (404 if none)
    4. `POST /session/visited-systems/reset-all` → clear all-time tracking
    5. `POST /session/start-session` → start new timestamped session (auto-stops previous if active)
    6. `POST /session/stop-session` → finalize active session (404 if none)
    7. `POST /session/visited-systems/toggle` → enable/disable all-time tracking (`{\"enabled\": bool}` JSON body)
- Diff: ~+580 / −0 (new module + integration points).
- Risk: medium (new persistence layer, log watcher integration, HTTP surface).
- Gates: build ✅ (Debug build successful, ef_overlay_helper.exe + ef_overlay_helper_common.lib) | tests ⚪ (manual smoke pending) | smoke ⏳ (requires live game + system jumps).
- Technical decisions:
  - **Persistence strategy:** JSON files for human readability + compatibility; schema version field for future migrations. Each session gets separate file to preserve historical play sessions without bloating all-time file.
  - **Session ID format:** `session_YYYYMMDD_HHMMSS` (ISO 8601 date + time, sortable, human-readable).
  - **Visit counter semantics:** First visit creates entry with visits=1; subsequent visits increment counter. System name stored with each entry for display (enables EF Map visualization without extra lookups).
  - **Log watcher integration point:** Existing overlay state publish callback (line ~428 in helper_runtime.cpp) now also records visits before ingesting state. Minimal performance impact (2 mutex operations per system jump, no blocking I/O in hot path - JSON write happens inside recordVisit with lock held).
  - **API authorization:** All endpoints require auth token (via existing authorize() mechanism in HelperServer). Write operations (reset, start/stop session, toggle) return 503 if SessionTracker unavailable.
  - **Error handling:** Comprehensive spdlog logging for all file I/O failures; endpoints return 500 + exception message on unexpected errors.
  - **Schema field exposure:** System data includes `{name, visits}` pairs keyed by system_id. Version field (`version: 1`) enables future format changes without breaking existing clients.
- JSON format examples:
  - All-time: `{\"version\":1,\"tracking_enabled\":true,\"last_updated_ms\":1729712345678,\"systems\":{\"sys123\":{\"name\":\"Mahnna\",\"visits\":5}}}`
  - Session: `{\"version\":1,\"session_id\":\"session_20251023_143022\",\"start_time_ms\":1729712345000,\"end_time_ms\":1729712567000,\"active\":false,\"systems\":{\"sys456\":{\"name\":\"Tanoo\",\"visits\":2}}}`
- Cross-repo: Will update EF-Map-main docs (DATA_EXPOSURE_PLAN.md) once web app integration starts; helper API contract now stable for polling.
- Follow-ups:
  - ⏳ Feature 1 overlay UI: Add checkbox (all-time toggle) + Start/Stop Session buttons
  - ⏳ Feature 1 web app: Create feature branch, implement helper polling, map visualization (halos/hover counts), Helper panel display
  - ⏳ Feature 3: Bookmark creation endpoints + UI
  - ⏳ Feature 4: Follow mode toggle UI polish
  - ⏳ Feature 2 enhancement: Auto-advance logic (deferred)

## 2025-10-23 – Phase 5 partial implementation: Next System in Route + Legacy Cleanup
- Goal: Implement Feature 2 (Next System in Route) and Feature 5 (Legacy Cleanup) from Phase 5 roadmap.
- Files:
  - Schema: `src/shared/overlay_schema.hpp`, `src/shared/overlay_schema.cpp` (RouteNode extended with planet_count, route_position, total_route_nodes)
  - Overlay UI: `src/overlay/overlay_renderer.cpp` (+Next System display, +Copy ID button, −debug buttons)
- Features implemented:
  - **Next System in Route display:** Shows upcoming waypoint with connection type (gate/jump), distance (ly), planet count, total nodes
  - **Copy System ID button:** Clipboard integration via Win32 API for easy in-game link creation
  - **Destination detection:** When one jump from end, displays "Destination: [System Name]" instead of next waypoint format
  - **Legacy cleanup:** Removed "Send waypoint advance event" and "Request follow toggle" debug buttons from Overview tab
- Diff: ~+120 / −45 (schema extensions, UI additions, button removals).
- Risk: low (UI-only changes, schema backward compatible).
- Gates: build ✅ (Debug build succeeded, only harmless unused var warning) | tests ⚪ (manual smoke needed with live route) | smoke ⏳ (deferred until helper integration complete).
- Technical decisions:
  - **Auto-advance logic deferred:** Initial plan called for automatic waypoint advancement when follow mode detects system jump. Investigation revealed this requires deeper integration between log watcher player location tracking and helper server route storage. Deferred to next session; web app will set `active_route_node_id` initially.
  - **Schema backward compatibility:** New RouteNode fields (planet_count, route_position, total_route_nodes) are optional during parsing; existing routes without these fields will function correctly (values default to 0).
  - **Clipboard implementation:** Used Win32 `OpenClipboard()`/`SetClipboardData()` directly rather than ImGui clipboard helpers to ensure reliable system ID copying.
- UI format examples:
  - Next waypoint: "Mahnna (gate) — 3.47 ly | 5 planets | 12 nodes [Copy ID]"
  - Destination: "Destination: Tanoo [Copy ID]"
- Cross-repo: Will mirror roadmap update to `EF-Map-main` showing Features 2 & 5 complete, Features 1, 3, 4 in-progress or deferred.
- Follow-ups:
  - ⏳ Feature 1 (Visited Systems): JSON persistence, log watcher integration, helper API endpoints, overlay UI toggles
  - ⏳ Feature 3 (Bookmark Creation): Helper endpoint, EF Map worker `/api/overlay-bookmark`, overlay UI with tribe checkbox
  - ⏳ Feature 4 (Follow Mode Toggle): UI refinement in Overview tab (already functional, cosmetic polish only)
  - ⏳ Feature 2 auto-advance: Implement player location → route waypoint reconciliation in helper runtime
  - ❌ Remove notes section: Not found in current Overview tab implementation (already clean)

## 2025-10-23 – Combat telemetry implementation (Phase 3)
- Goal: Implement full combat telemetry tracking with dual-line sparkline visualization, session persistence, and hit quality analytics.
- Files: 
  - Parser: `src/helper/log_parsers.hpp`, `src/helper/log_parsers.cpp` (HitQuality enum, miss detection)
  - Aggregation: `src/helper/log_watcher.hpp`, `src/helper/log_watcher.cpp` (CombatTelemetryAggregator with hit quality counters)
  - Schema: `src/shared/overlay_schema.hpp`, `src/shared/overlay_schema.cpp` (combat session + hit quality fields)
  - Rendering: `src/overlay/overlay_renderer.hpp`, `src/overlay/overlay_renderer.cpp` (dual-line sparkline, ~144px height)
- Features implemented:
  - **Dual-line sparkline:** Orange (dealt damage) + red (taken damage), 2-minute window, ~144px height (2x mining sparkline for better combat visibility)
  - **DPS calculation:** 10-second rolling window for smooth DPS curves showing weapon fire patterns
  - **Activity detection:** 2-second tail-off when combat ends (drops to zero if no damage change in 2s)
  - **Session tracking:** sessionStartMs, sessionDurationSeconds persisted across helper restarts
  - **Hit quality tracking:** Miss, Glancing, Standard, Penetrating, Smashing tracked separately for dealt/taken (10 counters total)
  - **Hover tooltips:** Show time ago (t-Xs) + both DPS values anywhere on sparkline
  - **Reset button:** Clears session totals and history
- Diff: ~+850 / −50 (new data structures, parser extensions, rendering logic, schema fields).
- Risk: medium (new telemetry pipeline + complex sparkline rendering).
- Gates: build ✅ | tests ⚪ (manual live combat validation) | smoke ✅ (multiple combat sessions tested, all features working).
- Technical challenges & solutions:
  1. **Backwards time display:** Fixed by anchoring samples to `now_ms()` and plotting backwards (t-0s right, t-120s left)
  2. **Jerky discrete movement:** Fixed by plotting raw data points directly (no interpolation), letting ImGui handle smooth rendering
  3. **20-30s tail-off delay:** Fixed with 2-second recent activity window (instant zero when damage stops changing)
  4. **Bouncing peaks (rescaling):** Fixed with stable peak tracking using 1% per second decay, never decaying below observed peak
  5. **Oscillating sharp peaks (3-4Hz vibration):** Root cause was interpolation at moving anchor creating synthetic values that fluctuated frame-to-frame. Fixed by removing interpolation entirely and plotting raw `combatDamageValues_` data points directly. X positions shift smoothly (scrolling effect), Y values locked to stable DPS data (zero oscillation).
  6. **Miss detection:** Misses use different log patterns than hits ("you miss X" vs "you hit X"). Added miss-specific pattern detection before normal damage parsing.
- Hit quality parser patterns:
  - Outbound misses: "you miss [target]", "your [weapon] misses [target]"
  - Inbound misses: "[attacker] misses you", "[attacker] miss you"
  - Quality keywords: "miss", "glanc" (glancing), "penetrat" (penetrating), "smash" (smashing)
- UI display format:
  - Combat totals: "15268.0 dealt | 2449.0 taken"
  - Hits dealt: "60 (9 pen, 13 smash, 31 std, 7 glance) | 0 miss"
  - Hits taken: "109 (35 pen, 21 smash, 42 std, 11 glance) | 0 miss"
  - Session: "2.8 min (started 2.8 min ago)"
  - Current DPS: "0.0 DPS dealt | 26.3 DPS taken"
  - Peak: "206.9 DPS"
- Cross-repo: None (overlay-only feature; combat log parsing independent of EF-Map-main).
- Follow-ups: ✅ All complete
  - ✅ Update GAME_OVERLAY_PLAN.md Phase 3 status (queued → complete)
  - ✅ Document combat architecture in LLM_TROUBLESHOOTING_GUIDE.md Section 8
  - ❌ Session persistence intentionally deferred (operator decision: reset button clears in-memory state; lightweight overlay use case doesn't require disk persistence)

## 2025-10-23 – Phase 3 combat telemetry completion
- Goal: Mark Phase 3 complete in roadmap and close outstanding follow-ups.
- Files: `docs/initiatives/GAME_OVERLAY_PLAN.md`, `docs/decision-log.md`.
- Rationale: Combat telemetry feature is complete for overlay use case. Reset button clears in-memory session state as designed; intentionally lightweight for combat site DPS monitoring. Session persistence (`combat_session.json`) and file deletion on reset are unnecessary for the overlay's current scope—players just need real-time DPS visibility and hit quality feedback during active combat sites. More extensive combat analytics may be surfaced in EF-Map web app later if needed.
- Diff: ~+15 / −10 (status update, clarified scope).
- Risk: low (documentation only).
- Gates: build n/a | tests n/a | smoke n/a.
- Cross-repo: Will mirror roadmap update to `EF-Map-main/docs/initiatives/GAME_OVERLAY_PLAN.md` in next sync.
- Follow-ups: None; Phase 3 complete, ready to proceed with Phase 5 (bookmarks/route UX) or Phase 6 (packaging) when scheduled.

## 2025-10-23 – Increase mining decay hold period for large laser compatibility
- Goal: Prevent jaggy sparkline when players use large mining lasers (6-second cycle time) by extending the hold period before decay starts.
- Files: `src/overlay/overlay_renderer.cpp`, `docs/LLM_TROUBLESHOOTING_GUIDE.md`.
- Change: Increased `kMiningCycleMs` from 4000ms (4s) to 7000ms (7s) to accommodate 6s large laser activation cycle plus 1s safety margin.
- Rationale:
  - Current 4s hold works for standard lasers (4s cycle) but would cause premature decay with large lasers (6s cycle).
  - With 4s hold + 6s laser cycle: decay would start at T+4s, new event arrives at T+6s → sawtooth pattern.
  - With 7s hold + 6s laser cycle: full cycle covered, smooth behavior maintained.
  - Safety margin (+1s) accounts for network latency, log write delays, and frame timing variations.
- Impact: 
  - Small lasers (4s): Extra 3s hold is imperceptible, maintains smooth appearance.
  - Large lasers (6s): Full cycle coverage prevents premature decay.
  - Total decay: Still 10s window (7s hold + 3s linear decay to zero).
- Diff: ~+3 / −2 (constant update + documentation).
- Risk: low (timing adjustment only, no behavioral changes for existing 4s lasers).
- Gates: build pending | tests ⚪ (manual validation) | smoke pending (test with 4s lasers to confirm no regression).
- Cross-repo: None (overlay-only timing constant).
- Follow-ups: Test with actual large mining lasers when available (est. 2-3 months); monitor for any edge cases.

## 2025-10-23 – Mining sparkline EMA smoothing & session persistence finalization
- Goal: Eliminate sparkline jitter during active mining and ensure session totals persist correctly across helper restarts and combat log file switches.
- Files: `src/overlay/overlay_renderer.cpp` (EMA smoothing), `src/helper/log_watcher.cpp` (session restore).
- Approach:
  - **EMA smoothing (α=0.3):** Applied to local copy of rate values before rendering to produce smooth oscilloscope-like curves during mining. Original persistent array untouched to preserve raw data.
  - **Session persistence fix:** Added `restoreSession()` call in `refreshCombatFile()` to reload `totalVolume_` and `sessionStart_` after aggregator reset, ensuring accumulation continues from saved state.
  - **Decay visualization:** Rejected synthetic decay sample generation (Option 2) after it broke active mining sparklines by blocking new samples. Kept interpolation-based decay (Option 1): smooth 10s decay rendered via lambda, transitions to accurate vertical drop after scrolling left.
- Diff: ~+40 / −15 (EMA loop in renderer, restore call in log watcher, removed Option 2 experimental code).
- Risk: medium (affects live mining display + persistence logic).
- Gates: build ✅ | tests ⚪ (manual validation only) | smoke ✅ (live mining sessions confirmed smooth curves, session persistence working, decay accurate).
- Decision rationale: 
  - Option 1 (interpolation-based decay) chosen over Option 2 (synthetic samples) because Option 2 inadvertently blocked regular sample updates during mining, causing sparkline to flatline. Option 1 provides factually accurate visualization (instant stop → vertical drop after scrolling) without side effects.
  - EMA smoothing eliminates high-frequency jitter while preserving peak information and session totals.
- Cross-repo: None (overlay-only changes).
- Follow-ups: Document EMA smoothing and decay behavior in LLM troubleshooting guide; monitor for any edge cases with rapid start/stop cycles.

## 2025-10-22 – Comprehensive overlay architecture documentation
- Goal: Create technical reference document explaining overlay system architecture, IPC mechanisms, browser communication patterns, and adaptation guide for reuse by other EVE Frontier DApps (requested by colleague).
- Files: `docs/OVERLAY_ARCHITECTURE.md` (new).
- Diff: ~+650 / −0 (architecture diagrams, component breakdown, schema reference, IPC deep-dive, browser bridge patterns, build instructions, troubleshooting, adaptation guide).
- Risk: low (documentation only).
- Gates: build n/a | tests n/a | smoke n/a.
- Cross-repo: None (overlay-specific documentation; browser integration patterns reference EF-Map-main).
- Follow-ups: Update when schema bumps to v5, add packaging/distribution sections once Phase 6 lands, mirror key sections in README for discoverability.

## 2025-10-15 – Orange HUD palette restoration
- Goal: Bring back the warm orange HUD palette and sparkline tint while keeping the base window glass dark and transparent to match the EVE Frontier UI.
- Files: `src/overlay/overlay_renderer.cpp`.
- Diff: ~+50 / −50 (color constants, accent/resize grip palette, ellipsis tint + ordering, glass transparency tweak, brighter orange match, sparkline inherits focus alpha).
- Risk: low (styling only).
- Gates: build ✅ (`cmake --build build --config RelWithDebInfo`).
- Follow-ups: Capture in-game screenshots to confirm the focused transparency still reads correctly across bright backgrounds.

- Goal: Capture the latest roadmap status (follow mode shipped, mining telemetry validating, star map on hold), expand the README with a roadmap snapshot + Azure signing lean, add an overlay-specific troubleshooting guide wired into guardrail docs, and reinforce smoke-test guidance (external PowerShell helper launch, inject via `exefile.exe`).
- Files: `docs/initiatives/GAME_OVERLAY_PLAN.md`, `README.md`, `docs/LLM_TROUBLESHOOTING_GUIDE.md`, `AGENTS.md`, `.github/copilot-instructions.md`.
- Diff: ~+320 / −40 (progress summary + phase status, README status table, new troubleshooting guide, guardrail references, smoke-test notes).
- Risk: low (documentation only).
- Gates: build n/a | tests n/a | smoke n/a.
- Cross-repo: Mirror the plan and guardrail references in `EF-Map-main` (pending next docs sync).
- Follow-ups: Update `EF-Map-main/docs/initiatives/GAME_OVERLAY_PLAN.md` plus guardrail docs with matching language and revisit the troubleshooting guide after mining telemetry ships.

## 2025-10-14 – Overlay renderer lifecycle restore & telemetry reset helper
- Goal: Reintroduce the renderer scaffolding lost during the mining sparkline refactor, restore the mining history constants, and route the telemetry reset button through a dedicated helper so the overlay builds again.
- Files: `src/overlay/overlay_renderer.cpp`, `src/overlay/overlay_renderer.hpp`.
- Diff: ~+150 / −110 (includes, lifecycle stubs, mining history constants, telemetry reset result struct, worker thread helper).
- Risk: medium (core overlay renderer behaviour + background thread wiring).
- Gates: build ✅ (`cmake --build build --config RelWithDebInfo` via MSBuild) | tests ⚪ (not rerun; combat parser fix still pending) | smoke ⏳ (requires reinjection during mining session).
- Cross-repo: None.
- Follow-ups: Teach the helper/runtime side to handle the new `telemetry_reset` event path and confirm the overlay toast renders during live mining.

## 2025-10-14 – Mining sparkline resample & smoothing
- Goal: Keep the mining rate sparkline aligned to a fixed 120 s window, remove start-up spikes, and render a smoother trace between helper updates.
- Files: `src/overlay/overlay_renderer.cpp`.
- Diff: ~+190 / −160 (interpolated volume lookup, fixed-window resample loop, hover lookup rewrite).
- Risk: medium (changes to mining HUD math and draw path inside injected overlay).
- Gates: build ✅ (`cmake --build build --config RelWithDebInfo`) | tests ⚪ (not re-run; telemetry harness still pending) | smoke ⏳ (awaiting live mining session with new DLL).
- Cross-repo: None (helper/shared schema untouched).
- Follow-ups: Observe in-client mining sessions for any lingering hover gaps or rate spikes; adjust resample cadence if performance issues appear.

## 2025-10-14 – Mining telemetry session surfaces
- Goal: Add per-ore session aggregation plus HUD and tray affordances so Phase 2 mining telemetry is observable and resettable end-to-end.
- Files: `src/helper/log_watcher.{hpp,cpp}`, `src/shared/overlay_schema.{hpp,cpp}`, `src/helper/helper_runtime.cpp`, `src/helper/tray_application.cpp`, `src/overlay/overlay_renderer.cpp`, `src/overlay/CMakeLists.txt`.
- Diff: ~+260 / −90 (aggregator timing + bucket sorting, schema plumbing, overlay histogram/reset control, tray summary tweak, new httplib link).
- Risk: medium (log parsing + injected HUD behaviour + helper HTTP integration).
- Gates: build ✅ (`cmake --build build --target ef_overlay_module`) | tests ⚪ (not run; telemetry tests still pending) | smoke ⏳ (awaiting in-game mining session to verify reset + charts).
- Cross-repo: None (Cloudflare/web helper wiring to follow in EF-Map-main after local telemetry bake).
- Follow-ups: Exercise mining loops in-game to confirm histogram + reset UX, then surface the same aggregations in the EF-Map helper panel.

## 2025-10-14 – Packaging roadmap staged for later
- Goal: Document the future installer/tray/protocol workflow inside the shared overlay plan so packaging work is ready once the helper features stabilize.
- Files: `docs/initiatives/GAME_OVERLAY_PLAN.md`, `docs/decision-log.md` (mirrors `EF-Map-main`).
- Diff: +76 / -0 (Phase 6 expanded with installer tech selection, signing, tray conversion, protocol handler, and update manifest steps).
- Risk: low (documentation only).
- Gates: build n/a | tests n/a | smoke n/a.
- Cross-repo: Same update applied to `EF-Map-main/docs/initiatives/GAME_OVERLAY_PLAN.md` with matching decision log entry.
- Follow-ups: Revisit once telemetry/follow mode milestones land to choose installer tech, purchase signing cert, and schedule implementation tasks.

## 2025-10-13 – Overlay HUD telemetry readout
- Goal: Surface helper-computed combat DPS and mining yield metrics directly in the overlay window so we can observe log-driven telemetry in real time.
- Files: `src/overlay/overlay_renderer.cpp`.
- Diff: ~+70/−0 (new Telemetry section with combat/mining summaries, recent windows, and last-event timers).
- Risk: medium (UI changes in injected ImGui HUD).
- Gates: build ✅ (`cmake --build build --config Debug`) | tests ❌ (`build/tests/Debug/ef_overlay_tests.exe` – existing combat damage parsing expectation) | smoke ⚪ (awaiting in-game inject with live logs).
- Cross-repo: None.
- Follow-ups: Fix the combat damage parser regression flagged by the unit test, then capture screenshots of the telemetry HUD once in-game smoke is possible.

## 2025-10-13 – Overlay styling hotfix (foreground accent rollback)
- Goal: Revert the foreground draw-list accent experiment that froze the client on injection while keeping the white resize highlight and top accent.
- Files: `src/overlay/overlay_renderer.cpp`.
- Diff: ~+30/−48 (remove foreground draw calls, trim extra style pushes, restore window draw-list accent with brighter colors, add separator overrides + clip guard, align top accent thickness with hover highlight).
- Risk: low (UI cosmetics only; regression fix).
- Gates: build ✅ (`cmake --build build --config Debug`) | tests ✅ (`ctest -C Debug --output-on-failure`) | smoke ⏳ (pending user validation post-injection).
- Cross-repo: None.
- Follow-ups: Revisit top accent approach later—consider dedicated overlay layer or viewport hook rather than foreground draw list.

## 2025-10-13 – Helper auto-detects EVE client process
- Goal: Resolve the target game client automatically so helper-triggered injections no longer require a manual PID lookup.
- Files: `src/helper/helper_runtime.cpp`.
- Diff: ~+120/−0 (process enumeration helper, UTF-8 conversion, injection messaging tweaks).
- Risk: low (helper-side process spawning only).
- Gates: build ✅ (`cmake --build build --config Debug`) | tests ✅ (`ctest -C Debug`, reran flaky queue test) | smoke ⏳ (manual helper inject next session).
- Cross-repo: None.
- Follow-ups: Point release of helper once Release binaries regenerated; consider adding retry loop to watch for client launch.

## 2025-10-13 – Overlay styling polish (accent + resize parity)
- Goal: Force the resize edge highlight to match the in-game white glow and ensure the active window draws a visible top accent.
- Files: `src/overlay/overlay_renderer.cpp`.
- Diff: ~+35/−8 (additional ImGui color overrides, foreground accent draw call).
- Risk: low (UI cosmetics only).
- Gates: build ✅ (`cmake --build build --config Debug`) | tests ✅ (`ctest -C Debug --output-on-failure`) | smoke ⏳ (user verifying in live client after reinject).
- Cross-repo: None.
- Follow-ups: Confirm accent visibility across multiple resolutions and tweak inactive accent tone once HUD palette is finalized.

## 2025-10-13 – Overlay window styling parity
- Goal: Restyle the ImGui debug window so it matches EVE Frontier UI (white active accent, neutral resize hints, ellipsis menu placeholder).
- Files: `src/overlay/overlay_renderer.cpp`.
- Diff: ~+120/−35 (style pushes, custom accent drawing, ellipsis glyph, follow-up tuning for accent thickness/hover behavior).
- Risk: low (UI cosmetics only).
- Gates: build ✅ (`cmake --build build --config Debug`) | tests ✅ (`ctest -C Debug`).
- Cross-repo: None.
- Follow-ups: Wire ellipsis menu into upcoming context actions; revisit colors once final HUD palette is locked.

## 2025-10-12 – Star catalog name fallback for map view
- Goal: Allow map rendering when helper routes carry system names instead of numeric IDs by indexing the star catalog by normalized name and teaching the renderer to fall back accordingly.
- Files: `src/shared/star_catalog.{hpp,cpp}`, `src/overlay/starfield_renderer.cpp`.
- Diff: ~+150/−10 (catalog index + renderer fallback + projection helper adjustments).
- Risk: medium (touches renderer hot path and catalog loader).
- Gates: build ✅ (`cmake --build build --config Release`) | tests ✅ (`ctest -C Release`) | smoke ✅ (helper restarted, overlay reinjected, live client ready for user verification).
- Cross-repo: None (catalog binary already shared with EF-Map main).
- Follow-ups: Monitor for ambiguous name collisions; if collisions appear, extend resolver with manual overrides and surface fallback status in helper diagnostics.

## 2025-10-12 – Overlay map HUD toggle
- Goal: Introduce map view HUD with route markers/labels and keep debug panel accessible via F7.
- Files: `src/overlay/overlay_renderer.cpp`
- Diff: ~240 ++ / 240 --
- Risk: medium
- Gates: build ✅ (Release) tests ✅ (ctest Release) smoke ❌ (manual game attach deferred)
- Cross-repo: EF-Map-main map view branch (starfield renderer helpers)
- Follow-ups: Run in-game smoke to verify markers and labels align with starfield focus once helper is reattached.

## 2025-10-12 – Map/debug window unification & catalog packaging
- Goal: Ensure map/debug modes reuse the same ImGui window with resizing preserved and deploy `star_catalog_v1.bin` alongside the injected DLL so the starfield renderer initializes.
- Files: `src/overlay/overlay_renderer.cpp`, runtime asset copy to `build/src/overlay/Release/`.
- Diff: ~180 ++ / 220 --
- Risk: low
- Gates: build ⚪ (Release blocked by DLL in use) | build ✅ (Debug) | tests ✅ (`ctest -C Debug --output-on-failure`) | smoke ✅ (live client confirmed shared window + starfield ready after reinject)
- Cross-repo: Asset mirrors `EF-Map-main/data/star_catalog_v1.bin`; no code changes required in main repo.
- Follow-ups: Re-run Release build once DLL is unloaded, then ship new binary bundle; automate asset copy in build script so future releases include the catalog.
<!-- Overlay decision log created 2025-09-26. Mirror significant cross-repo events with `EF-Map-main/docs/decision-log.md` (sibling repository) and include a `Cross-repo` note per entry when applicable. -->

## 2025-10-12 – Release smoke test (helper + injector)
- Goal: Validate the end-to-end overlay flow (helper HTTP API, shared-memory bridge, injector, in-game rendering) using the refreshed star catalog data.
- Files: docs only (`README.md`).
- Diff: README updated to document the canonical EVE Frontier process name `exefile.exe` and revised injection instructions.
- Risk: low (documentation + runtime validation).
- Gates: build ✅ (Release artifacts from same session) | tests ⚪ (no new code) | smoke ✅ (helper state + in-game overlay screenshot).
- Cross-repo: Informational only; no `EF-Map-main` changes required.
- Follow-ups: Replace debug ImGui panel with actual map visuals, script helper shutdown for automated runs, and schedule the next smoke once log watcher routes feed the overlay.

## 2025-10-13 – Overlay roadmap alignment: local telemetry focus
- Goal: Synchronize the roadmap with the clarified local-only telemetry plan—acknowledging the existing chat log parser, staging follow mode, mining/combat graphs, and packaging as sequenced phases.
- Files: `docs/initiatives/GAME_OVERLAY_PLAN.md` (mirrored in EF-Map main).
- Diff: documentation updates (revised progress summary, new phased roadmap, refreshed next steps list).
- Risk: low (planning alignment).
- Gates: build ⚪ | tests ⚪ | smoke ⚪ (no runtime changes).
- Cross-repo: Logged in `EF-Map-main/docs/decision-log.md` with matching title/date.
- Follow-ups: Close out Phase 1 polish, then begin Phase 2 mining telemetry implementation using live logs.

## 2025-10-12 – Helper heartbeat + overlay auto-hide
- Goal: Hide the in-game overlay automatically when the helper stops (graceful exit or crash) and revive it when the heartbeat resumes.
- Files: `src/shared/overlay_schema.{hpp,cpp}`, `src/helper/helper_server.{hpp,cpp}`, `src/helper/helper_runtime.cpp`, `src/helper/log_watcher.cpp`, `src/overlay/overlay_renderer.{hpp,cpp}`, `tests/overlay_tests.cpp`, `README.md`.
- Diff: ~+220/−40 across schema, helper, renderer, tests, and docs.
- Risk: medium (touches shared-memory schema and injected render path).
- Gates: build ✅ (`cmake --build build --config Release`) | tests ✅ (`ctest -C Release --output-on-failure`) | smoke ✅ (helper forced-stop then restart in live client auto-hid and auto-showed overlay).
- Cross-repo: None (heartbeat metadata is overlay-only at this stage).
- Follow-ups: Re-run in-game smoke with the new DLL, monitor heartbeat timeout (5s) during live play, and surface heartbeat status in the upcoming helper tray UI.

## 2025-10-12 – Camera-aligned starfield + route polyline
- Goal: Align the DX12 starfield with helper-provided camera pose and render live route polylines so the in-game overlay mirrors EF-Map navigation.
- Files: `src/overlay/starfield_renderer.{hpp,cpp}`, `src/overlay/overlay_hook.cpp`.
- Diff: ~+520/−180 lines (constant-buffered renderer, dynamic route buffer, hook wiring).
- Risk: medium (new GPU constant buffer updates and dynamic route uploads in injected process).
- Gates: build ✅ (`cmake --build build --config Debug`) | tests ✅ (`build/tests/Debug/ef_overlay_tests.exe`).
- Cross-repo: Documented in `EF-Map-main/docs/decision-log.md` (2025-10-12 – Overlay camera-aligned renderer sync).
- Follow-ups: Add waypoint markers/selection glow, profile GPU/frame impact in live sessions, and expose renderer health metrics in helper tray diagnostics.

## 2025-10-13 – Overlay roadmap refocus (helper-first)
- Goal: Pause native starfield visualization work and prioritize helper-driven features (log watcher, mining/DPS tracking, in-overlay actions) that deliver immediate value.
- Files: docs only (`docs/decision-log.md`, `docs/initiatives/GAME_OVERLAY_PLAN.md`).
- Diff: n/a (doc updates).
- Risk: low (strategic reprioritization).
- Gates: build ⚪ | tests ⚪ | smoke ⚪ (no code path changes).
- Cross-repo: Mirrored in `EF-Map-main/docs/decision-log.md` with same title/date.
- Follow-ups: Execute helper-first roadmap—ship log watcher + position sync, tray UX shell, session tracking modules, and browser CTA/event bridge before revisiting starfield polish.

## 2025-10-13 – DPS/mining telemetry roadmap breakdown
- Goal: Document the phased DPS/mining telemetry rollout (log parsing, real-time aggregators, schema/UI wiring, optional web bridge) while constraining scope to personal character data.
- Files: docs only (`docs/initiatives/GAME_OVERLAY_PLAN.md`).
- Diff: n/a (documentation elaboration).
- Risk: low (planning-only change).
- Gates: build ⚪ | tests ⚪ | smoke ⚪ (no runtime changes).
- Cross-repo: Mirrored in `EF-Map-main/docs/decision-log.md` with matching summary.
- Follow-ups: Implement helper parser extensions, aggregator publishing, overlay HUD telemetry, and diagnostics per the documented phases.

## 2025-10-02 – Native starfield renderer spike (DX12 point cloud)
- Goal: Render the EF-Map star catalog inside the overlay using a lightweight DX12 pipeline (point sprites + additive blend) as the baseline for native visuals.
- Files: `src/overlay/starfield_renderer.{hpp,cpp}`, `src/overlay/overlay_hook.cpp`, `src/overlay/CMakeLists.txt`, `src/overlay/overlay_renderer.hpp` (indirect include), build regeneration.
- Diff: +2 new overlay source files (~420 LoC) plus ~+140/−15 adjustments across the DX12 hook and build wiring.
- Risk: medium (new GPU pipeline + runtime asset load in injected process).
- Gates: build ✅ (`cmake --build build --config Debug --target ef_overlay_tests`) | tests ✅ (`ctest --test-dir build -C Debug --output-on-failure`).
- Cross-repo: EF-Map main initiative plan updated (2025-10-02 – Native starfield renderer spike) to mark milestone progress.
- Follow-ups: Integrate overlay camera transforms, render route polylines from shared state, tune colors/brightness for parity with web map, and profile GPU cost in live client sessions.

## 2025-10-02 – Star catalog asset loader + helper metadata
- Goal: Load the exported EF-Map star catalog inside the helper runtime, expose catalog metadata via HTTP/status APIs, and add a shared parser + tests so the overlay can consume the binary format.
- Files: `src/shared/star_catalog.{hpp,cpp}`, `src/shared/CMakeLists.txt`, `tests/overlay_tests.cpp`, `src/helper/helper_runtime.{hpp,cpp}`, `src/helper/helper_server.{hpp,cpp}`, `CMakeLists.txt` (indirect via shared lib), `build` regeneration (cmake configure).
- Diff: +2 new source files (~310 LoC) plus ~+190/−20 changes across helper runtime/server, tests, and build wiring.
- Risk: medium (helper startup now depends on catalog asset; new HTTP surface for metadata).
- Gates: build ✅ (`cmake --build build --config Debug --target ef_overlay_tests`) | tests ✅ (`ctest --test-dir build -C Debug --output-on-failure`).
- Cross-repo: EF-Map main decision log (2025-10-02 – Star catalog exporter + overlay asset copy).
- Follow-ups: Feed catalog into the DX12 renderer, extend asset format with stargate adjacency once overlay requirements stabilize, and surface catalog telemetry in the upcoming tray UI.

## 2025-10-01 – Helper WebSocket hub + EF-Map bridge handshake
- Goal: Promote the helper’s WebSocket hub (real-time overlay state/events) and finalize the browser handshake so EF-Map consumes live payloads without HTTP polling.
- Files: `src/helper/helper_websocket.{hpp,cpp}`, `src/helper/helper_server.{hpp,cpp}`, `cmake/Dependencies.cmake`, `src/helper/CMakeLists.txt`, helper docs (minor).
- Diff: +2 new source files (~560 LoC) plus ~+220/−80 adjustments to helper server lifecycle and CMake dependency wiring.
- Risk: medium (long-lived network listeners + broadcast threading).
- Gates: build ✅ (`cmake --build build --config Release`) | tests ✅ (`ctest -C Release`) | smoke ⏳ (tray/WebSocket badge documentation next).
- Cross-repo: EF-Map main decision log (2025-10-01 – Helper WebSocket bridge + frontend badge).
- Follow-ups: Surface helper tray/Web UI indicators, add WebSocket smoke script, and expand unit coverage for handshake failure paths.

## 2025-09-26 – Repository bootstrap & guardrails mirror
- Goal: Establish baseline guardrails for the EF-Map overlay helper repository and mirror shared documentation from the main EF-Map project.
- Files: `AGENTS.md`, `.github/copilot-instructions.md`, `docs/decision-log.md`, `docs/initiatives/GAME_OVERLAY_PLAN.md`, workspace README.
- Diff: +4 files (docs only).
- Risk: low (documentation setup).
- Gates: build ⏳ (tooling not yet established) | tests ⏳ | smoke ⏳
- Cross-repo: EF-Map main decision log (2025-09-26) – note referencing overlay repo bootstrap.
- Follow-ups: Populate helper/overlay source structure; document build/test commands once code exists.

## 2025-09-26 – Source scaffolding & roadmap alignment
- Goal: Create initial filesystem layout for helper, overlay, and tooling modules with placeholder documentation outlining responsibilities.
- Files: `src/helper/README.md`, `src/overlay/README.md`, `tools/README.md`, repository `README.md` update.
- Diff: +3 files, README update (docs only).
- Risk: low (structure + documentation).
- Gates: build n/a | tests n/a | smoke n/a (no executable code yet).
- Cross-repo: Refer to EF-Map main decision log (2025-09-26) for coordination context; no code impact on main repo.
- Follow-ups: Decide implementation language + build system, add initial build/test scripts, begin helper prototype.

## 2025-09-26 – CMake build scaffold + stubs
- Goal: Introduce a Windows-focused CMake build system with minimal helper executable and overlay DLL placeholders.
- Files: `CMakeLists.txt`, `src/helper/CMakeLists.txt`, `src/helper/main.cpp`, `src/overlay/CMakeLists.txt`, `src/overlay/dllmain.cpp`, `.gitignore`, `tools/README.md`, root `README.md` build section.
- Diff: +5 source files, README/tooling updates.
- Risk: low (build scaffold only).
- Gates: build ⚪ (not executed yet) | tests ⚪ | smoke ⚪ (no runtime code beyond stubs).
- Cross-repo: Informational only; no main repo impact yet.
- Follow-ups: Flesh out helper protocol listener, add overlay rendering harness, integrate automated build in CI.

## 2025-09-26 – Native helper stack decision
- Goal: Lock helper implementation to a native C++20 toolchain and enumerate core dependencies for upcoming work.
- Files: `README.md` (build status + next steps).
- Diff: documentation update only.
- Risk: low.
- Gates: build n/a | tests n/a | smoke n/a.
- Cross-repo: No immediate changes in EF-Map main; coordinate once helper API endpoints are exposed.
- Follow-ups: Integrate MinHook, spdlog, cpp-httplib, nlohmann/json, ImGui into the CMake pipeline.

## 2025-09-26 – Helper HTTP stub + overlay renderer loop
- Goal: Stand up a localhost API skeleton for the helper and exercise a background ImGui loop in the overlay module.
- Files: `src/helper/helper_server.{hpp,cpp}`, `src/helper/main.cpp`, `src/helper/CMakeLists.txt`, `src/overlay/overlay_renderer.{hpp,cpp}`, `src/overlay/dllmain.cpp`, `src/overlay/CMakeLists.txt`, `README.md`, `src/helper/README.md`, `src/overlay/README.md`, `cmake/Dependencies.cmake` (minor policy tweak), configure/build commands rerun.
- Diff: +4 source files, substantial updates to helper/overlay entry points and documentation.
- Risk: medium (introduces threaded HTTP listener and ImGui lifecycle management).
- Gates: build ✅ (`cmake --build ... --config Debug`) | tests ⚪ (not yet implemented) | smoke ⚪ (manual runtime validation pending DirectX hook).
- Cross-repo: EF-Map main unaffected; integration hooks will follow once API contracts solidify.
- Follow-ups: Expose authenticated command endpoints, wire IPC to overlay thread, and connect render loop to actual DX12 swap-chain via MinHook.

## 2025-09-26 – Protocol handler + authenticated command intake
- Goal: Register the `ef-overlay://` protocol on Windows, enforce a shared-secret on helper APIs, and support forwarding commands via HTTP or direct invocation.
- Files: `src/helper/main.cpp`, `src/helper/helper_server.{hpp,cpp}`, `src/helper/protocol_registration.{hpp,cpp}`, `src/helper/CMakeLists.txt`, root `README.md`, `src/helper/README.md`.
- Diff: +2 source files, major updates to helper entry point and server auth flow, README additions.
- Risk: medium (registry writes, authentication gate).
- Gates: build ✅ (`cmake --build ... --config Debug`) | tests ⚪ | smoke ⚪ (manual click/URI test pending).
- Cross-repo: No immediate EF-Map main changes; overlay deep-link payload schema remains TODO.
- Follow-ups: Define structured payload schema feeding the overlay, bridge to DX12 hooks, and add CI/static analysis with third-party warning suppression.

## 2025-09-26 – Canonical overlay schema + shared memory bridge
- Goal: Establish a shared overlay payload schema, publish helper snapshots to shared memory, and surface the data in the overlay renderer.
- Files: `src/shared/{overlay_schema.cpp,overlay_schema.hpp,shared_memory_channel.cpp,shared_memory_channel.hpp,CMakeLists.txt}`, `src/helper/{helper_server.cpp,helper_server.hpp,main.cpp,README.md,CMakeLists.txt}`, `src/overlay/{overlay_renderer.cpp,overlay_renderer.hpp,README.md,CMakeLists.txt}`, root `README.md`, `CMakeLists.txt`.
- Diff: +1 implementation file, updates across helper/overlay runtime and documentation.
- Risk: medium (introduces IPC channel and background parsing/rendering).
- Gates: build ✅ (`cmake --build build --config Release`) | tests ⚪ (not yet implemented) | smoke ⚪ (DX12 integration pending; ImGui preview exercised).
- Cross-repo: EF-Map main unaffected for now; coordinate once web app emits payloads matching the schema.
- Follow-ups: Integrate DX12 swap-chain hook, add automated validation/tests for schema changes, document worker/client handshake once exposed to EF-Map main.

## 2025-09-26 – Overlay schema regression tests
- Goal: Add a lightweight CTest harness to exercise overlay schema parsing/serialization and shared-memory round-trips.
- Files: `CMakeLists.txt`, `tests/CMakeLists.txt`, `tests/overlay_tests.cpp`, root `README.md` (test instructions).
- Diff: +2 files, build/doc updates.
- Risk: low (test-only additions).
- Gates: build ✅ (`cmake --build build --config Release --target ef_overlay_tests`) | tests ✅ (`ctest -C Release --output-on-failure`) | smoke n/a.
- Cross-repo: None.
- Follow-ups: Expand coverage once DX12 hook lands (e.g., snapshot dispatch fuzzing), wire tests into future CI workflow.

## 2025-09-26 – DX12 swap-chain hook integration

## 2025-10-01 – Log watcher system ID resolver
- Goal: Resolve Local chat system names to canonical EF system IDs so overlay payloads match shared schema expectations.
- Files: `src/helper/{log_watcher.{hpp,cpp},system_resolver.{hpp,cpp},system_resolver_data.hpp,helper_runtime.{hpp,cpp},CMakeLists.txt}`, `tests/overlay_tests.cpp`.
- Diff: +3 source files, +1 generated header (~24k entries), updates to helper runtime/log watcher wiring, new unit tests.
- Risk: medium (large embedded dataset + runtime resolution affects log watcher output).
- Gates: build ✅ (`cmake --build build --config Release`) | tests ✅ (`ctest --test-dir build -C Release`) | smoke ⏳ (tray/runbook validation next session).
- Cross-repo: Dataset generated from `EF-Map-main/all_solarsystems.json`; no code changes required in main repo yet.
- Follow-ups: Expose helper event bridge to EF-Map client; handle duplicate system names (`D:28NL`, `Alghoarismi`) via manual mapping if needed.

## 2025-09-27 – DX12 overlay smoke test & queue capture fix
- Goal: Diagnose missing in-game overlay rendering, capture the game’s command queue reliably, add a visibility toggle, and verify the overlay UI inside the EVE Frontier client.
- Files: `src/overlay/overlay_hook.cpp`, `src/overlay/overlay_renderer.cpp`, `src/overlay/dllmain.cpp`, `tools/overlay_smoke.ps1`, docs update.
- Diff: ~+220/−90 lines (expanded logging, command-queue capture fix, visibility toggle, automation script).
- Risk: high (live process hooking, GPU command submission adjustments).
- Gates: build ✅ (`cmake --build build --config Release --target ef_overlay_module`) | tests ⚪ (not run; unchanged unit coverage) | smoke ✅ (manual injection with live client screenshot).
- Follow-ups: Polish overlay styling (remove remaining debug UI), evaluate long-run performance impact, surface helper command to toggle overlay remotely, and mirror summary in `EF-Map-main/docs/decision-log.md` once integration plan is drafted.

## 2025-09-27 – Overlay input capture & helper smoke script fixes
- Goal: Allow the overlay window to intercept keyboard/mouse input (F8 toggle, drag-to-move without pass-through), enable resize-from-edge controls, and align the smoke-test script with the helper’s runtime port.
- Files: `src/overlay/overlay_hook.cpp`, `src/overlay/overlay_renderer.cpp`, `tools/overlay_smoke.ps1`.
- Diff: ~+155/−40 lines (Win32 WndProc hook, ImGui IO config, input swallow helpers, resize-from-edge flag, port/env handling tweaks).
- Risk: high (window procedure interception inside game process).
- Gates: build ✅ (`cmake --build build --config Release --target ef_overlay_module`) | tests ⚪ (unchanged) | smoke ⚪ (awaiting in-game verification post-build).
- Gates: smoke ✅ (manual injection via `tools/overlay_smoke.ps1` with helper payload and in-game verification for F8 toggle, drag, scroll, multi-corner resize).
- Follow-ups: Validate long-session stability and consider helper auto-shutdown flag in smoke script; document cross-repo impact once EF-Map main integrates overlay controls.

## 2025-10-01 – Roadmap refresh: log telemetry focus & HTML embed stance
- Goal: Elevate the game log watcher and combat telemetry overlay milestones, document required event-channel groundwork, and capture guidance on HTML overlay embedding.
- Files: `docs/initiatives/GAME_OVERLAY_PLAN.md`.
- Diff: Adjusted Phase 2 milestones (log watcher detail, new combat telemetry item), refined next steps, added HTML embedding cautionary note.
- Risk: low (documentation only).
- Gates: build n/a | tests n/a | smoke n/a.
- Cross-repo: EF-Map main decision log (2025-10-01 – Roadmap refresh: log telemetry focus & HTML embed stance).
- Follow-ups: Implement helper log watcher + location toggle, prototype combat telemetry overlay, revisit HTML embedding after native renderer spike.

## 2025-10-01 – Overlay schema v2 + event queue infrastructure
- Goal: Finalize the v2 overlay state contract (player marker, highlights, camera pose, HUD hints, follow flag) and stand up a shared-memory event queue with helper HTTP polling for overlay-generated actions.
- Files: `src/shared/overlay_schema.*`, `src/shared/event_channel.*` (new), `src/shared/CMakeLists.txt`, `src/helper/{helper_server.*,main.cpp}`, `src/overlay/overlay_renderer.*`, `tests/overlay_tests.cpp`, `docs/initiatives/GAME_OVERLAY_PLAN.md`.
- Diff: +2 new shared files (event ring buffer), ~+430/-60 lines updating schema, helper/overlay plumbing, and unit tests; plan updated to mark milestone complete.
- Risk: medium (shared-memory layout change + new cross-process queue).
- Gates: build ✅ (`cmake --build build --config Release`) | tests ✅ (`ctest -C Release`) | smoke ⏳ (needs in-game verification of new debug UI + event emission).
- Cross-repo: EF-Map main decision log (2025-10-01 – Overlay schema v2 + event queue infrastructure).
- Follow-ups: Bridge helper event queue to EF-Map web client, feed real payloads from log watcher once implemented, add automated smoke covering event drain endpoint.

## 2025-10-01 – Roadmap reprioritization: helper tray shell MVP
- Goal: Pull the helper tray shell forward in the roadmap to provide a tangible control surface before log watcher / event bridge work, documenting the new sequencing.
- Files: `docs/initiatives/GAME_OVERLAY_PLAN.md` (mirrored in EF-Map main).
- Diff: Added Section 7.0 for tray shell milestone, retitled packaging section, reordered “Next Steps.”
- Risk: low (documentation only).
- Gates: build n/a | tests n/a | smoke n/a.
- Cross-repo: EF-Map main decision log (2025-10-01 – Roadmap reprioritization: helper tray shell MVP).
- Follow-ups: Implement tray shell MVP, then resume log watcher and event bridge milestones as reordered.

## 2025-10-01 – Helper log watcher foundation
- Goal: Monitor live Local chat and combat logs to derive player location/combat activity and publish automatic overlay states from the helper runtime.
- Files: `src/helper/{helper_runtime.*,log_watcher.*,log_parsers.*,CMakeLists.txt}`, `tests/{CMakeLists.txt,overlay_tests.cpp}`, build scripts; added Windows API handling for default log paths.
- Diff: +4 new source files, ~+640/−20 lines touching helper runtime + tests.
- Risk: medium (multithreaded file tailing, shared-memory publication).
- Gates: build ✅ (`cmake --build build --config Debug`) | tests ✅ (`ctest --test-dir build -C Debug`) | smoke ⏳ (pending in-game validation once tray diagnostics land).
- Cross-repo: None yet (EF-Map main unchanged until event bridge wiring).
- Follow-ups: Surface watcher status in tray UI, map system names ↔ IDs, feed combat digest into overlay HUD hints, and schedule a smoke run with live log files.

### Launch steps (reference)
1. `cmake --build build --config Release --target ef_overlay_helper ef_overlay_injector ef_overlay_module`
2. Start the helper + injector: `.	ools\,overlay_smoke.ps1 -GameProcess "exefile.exe"`
	- Optional `-DetachHelper` to run helper in its own console, `-SkipPayload` to avoid posting the sample route.
3. In-game controls: **F8** hide/show, drag title bar to move, drag any edge/corner to resize, mouse wheel to scroll.
4. Stop helper when finished: `Get-Process ef-overlay-helper | Stop-Process`

 # #   2 0 2 5 - 1 0 - 2 7     H T T P   4 0 5   B u g   F i x e s   v i a   C h r o m e   D e v T o o l s   M C P   ( c r o s s - r e p o ) 
 -   G o a l :   R e s o l v e   w e b   a p p   H T T P   4 0 5   e r r o r s   b l o c k i n g   v i s i t e d   s y s t e m s   t r a c k i n g   f e a t u r e   ( w e b   U I     h e l p e r   i n t e g r a t i o n ) . 
 -   C r o s s - r e p o :   S e e   c : \ E F - M a p - m a i n \ d o c s \ d e c i s i o n - l o g . m d   f o r   f u l l   d e t a i l s   ( m a l f o r m e d   U R L   c o n s t r u c t i o n   +   d u p l i c a t e   C O R S   h e a d e r s ) . 
 -   H e l p e r   c h a n g e s :   F i x e d   d u p l i c a t e   C O R S   h e a d e r s   i n   h e l p e r _ s e r v e r . c p p   O P T I O N S   h a n d l e r   ( r e m o v e d   r e s . s e t _ h e a d e r   c a l l s ,   r e l y i n g   o n   s e t _ d e f a u l t _ h e a d e r s ) . 
 -   H e l p e r   r e b u i l t :   P I D   2 5 6 0 8   w i t h   C O R S   f i x ;   t r a c k i n g   t o g g l e   a n d   s t o p - s e s s i o n   e n d p o i n t s   v e r i f i e d   w o r k i n g   ( H T T P   2 0 0 ) . 
 -   M y s t e r y :   S t a r t - s e s s i o n   e n d p o i n t   r e t u r n s   H T T P   4 0 4   d e s p i t e   i d e n t i c a l   r e g i s t r a t i o n   a t   l i n e   8 9 2   ( s t o p - s e s s i o n   w o r k s   a t   l i n e   9 1 9 ) .   I n v e s t i g a t i o n   d e f e r r e d . 
 -   D o c u m e n t a t i o n :   A d d e d   C h r o m e   D e v T o o l s   M C P   p r o a c t i v e   g u i d a n c e   t o   . g i t h u b \ c o p i l o t - i n s t r u c t i o n s . m d   a n d   A G E N T S . m d   ( m i r r o r   o f   E F - M a p - m a i n   u p d a t e s ) . 
 -   I n i t i a t i v e :   U p d a t e d   G A M E _ O V E R L A Y _ P L A N . m d   P h a s e   5   F e a t u r e   1   p r o g r e s s   ( t r a c k i n g   t o g g l e   ,   s e s s i o n   U I   i m p r o v e m e n t s   ,   s t a r t - s e s s i o n   4 0 4   ) . 
 -   G a t e s :   h e l p e r   b u i l d     |   w e b   a p p   d e p l o y     ( f e a t u r e - o v e r l a y - v i s i t e d - s y s t . e f - m a p . p a g e s . d e v )   |   t r a c k i n g   t o g g l e   s m o k e     |   s e s s i o n   s t o p   s m o k e   . 
 
 
 