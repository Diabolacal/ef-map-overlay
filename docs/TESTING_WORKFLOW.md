# Testing Workflow for Debug Features

## The Problem

When testing features like the debug logging tray menu, we need to test the **actual tray application** (`ef-overlay-tray.exe`), not the console helper (`ef-overlay-helper.exe`).

**Current situation:**
- Store version: v1.0.2 installed via Microsoft Store (production)
- Debug builds: Need to test without interfering with production install
- Two executables exist:
  - `ef-overlay-helper.exe` - Console-based server (wmain) - opens PowerShell window
  - `ef-overlay-tray.exe` - GUI tray application (WinMain) - **what we need to test**

## Recommended Testing Strategy

### Option 1: Side-by-Side Testing (RECOMMENDED) ⭐

**Best for:** Quick iteration, frequent testing, minimal disruption

**How it works:**
- Keep Store version installed (production)
- Run Debug tray executable directly from build directory
- Both can coexist as long as only one runs at a time

**Steps:**

1. **Stop Store version helper:**
   ```powershell
   Get-Process -Name "ef-overlay-tray" -ErrorAction SilentlyContinue | Stop-Process -Force
   ```

2. **Build Debug tray executable:**
   ```powershell
   cd C:\ef-map-overlay\build
   cmake --build . --config Debug --target ef_overlay_tray
   ```

3. **Run Debug tray directly:**
   ```powershell
   cd C:\ef-map-overlay\build\src\helper\Debug
   .\ef-overlay-tray.exe
   ```
   
   This will:
   - Start the tray application (look for icon in system tray)
   - Load Debug binaries with your new features
   - Use same config/log locations as Store version (`%LOCALAPPDATA%\EFOverlay\`)
   - Allow right-click menu testing

4. **Test your feature:**
   - Right-click tray icon
   - Test new menu items
   - Verify functionality

5. **Stop Debug version, restart Store version:**
   ```powershell
   # Stop Debug version
   Get-Process -Name "ef-overlay-tray" -ErrorAction SilentlyContinue | Stop-Process -Force
   
   # Restart Store version (from Start Menu)
   Start-Process "shell:AppsFolder\Ef-Map.EF-MapOverlayHelper_r3vrm21jghstm!App"
   ```

**Pros:**
- ✅ Fast iteration (just rebuild + run)
- ✅ No certificate management
- ✅ No uninstall/reinstall cycles
- ✅ Keep production version for daily use

**Cons:**
- ⚠️ Must manually stop Store version first
- ⚠️ Doesn't test MSIX packaging, protocol registration, or AppContainer
- ⚠️ Shared config/logs between Debug and production

**When to use:** Daily development, testing tray menu changes, debugging features

---

### Option 2: Full MSIX Package Testing

**Best for:** Pre-release validation, protocol handler testing, AppContainer testing

**How it works:**
- Uninstall Store version temporarily
- Build & sign Debug MSIX package
- Install Debug MSIX
- Test as if it were production
- Uninstall Debug MSIX, reinstall Store version

**Steps:**

1. **Uninstall Store version:**
   ```powershell
   Get-AppxPackage "*EF-Map*" | Remove-AppxPackage
   ```

2. **Build Debug binaries:**
   ```powershell
   cd C:\ef-map-overlay\build
   cmake --build . --config Debug
   ```

3. **Package Debug MSIX:**
   ```powershell
   cd C:\ef-map-overlay\packaging\msix
   .\build_msix.ps1 -Version "1.0.3-debug" -BuildConfig "Debug"
   ```

4. **Sign for local testing:**
   ```powershell
   .\sign_for_local_testing.ps1 -MsixPath "C:\ef-map-overlay\releases\EFMapHelper-v1.0.3-debug.msix"
   ```

5. **Install certificate** (first time only):
   - Double-click `C:\ef-map-overlay\releases\EF-Map-Local-Test.cer`
   - Store Location: Current User
   - Place in: Trusted Root Certification Authorities

6. **Install Debug MSIX:**
   - Double-click `C:\ef-map-overlay\releases\EFMapHelper-v1.0.3-debug-SIGNED-LOCAL-TEST.msix`
   - Click "Install"

7. **Test:**
   - Launch from Start Menu: "EF Map Overlay Helper"
   - Test protocol handler: Visit web app, click overlay buttons
   - Test tray menu features
   - Verify AppContainer behavior

8. **Cleanup:**
   ```powershell
   # Uninstall Debug package
   Get-AppxPackage "*EF-Map*" | Remove-AppxPackage
   
   # Reinstall Store version
   # Go to Microsoft Store → Library → EF Map Overlay Helper → Install
   ```

**Pros:**
- ✅ Tests full deployment scenario
- ✅ Validates protocol registration
- ✅ Tests AppContainer permissions
- ✅ Catches packaging issues early

**Cons:**
- ❌ Slow iteration (package + sign + install each time)
- ❌ Requires certificate management
- ❌ Must uninstall/reinstall Store version
- ❌ More complex workflow

**When to use:** Pre-release validation, protocol handler changes, AppContainer debugging

---

### Option 3: Hybrid Approach (PRAGMATIC) ⭐⭐

**Best for:** Most development scenarios

**Strategy:**
1. **Daily feature work**: Use Option 1 (side-by-side)
2. **Pre-commit validation**: Quick Option 1 smoke test
3. **Pre-release**: Full Option 2 MSIX test before Store submission

**Workflow:**
```
Feature development → Option 1 testing (fast iteration)
    ↓
Ready to commit? → Option 1 final smoke test
    ↓
Ready for release? → Option 2 full MSIX validation
    ↓
Submit to Store
```

---

## Quick Reference Commands

### Stop Store Helper
```powershell
Get-Process -Name "ef-overlay-tray" -ErrorAction SilentlyContinue | Stop-Process -Force
```

### Build & Run Debug Tray
```powershell
cd C:\ef-map-overlay\build
cmake --build . --config Debug --target ef_overlay_tray
cd src\helper\Debug
.\ef-overlay-tray.exe
```

### Check Running Helper
```powershell
Get-Process -Name "ef-overlay-tray" -ErrorAction SilentlyContinue | Select-Object Id, ProcessName, Path
```

### Restart Store Helper
```powershell
Start-Process "shell:AppsFolder\Ef-Map.EF-MapOverlayHelper_r3vrm21jghstm!App"
```

---

## Updated Smoke Test Script

For **Option 1** (recommended for debug logging feature), use this updated script:

**Location:** `tools/test_debug_logging_tray.ps1`

```powershell
# Debug Logging Feature Smoke Test - Tray Application Version
# Tests actual tray executable (not console helper)

$ErrorActionPreference = "Continue"

Write-Host "=== Debug Logging Feature Smoke Test (Tray Application) ===" -ForegroundColor Cyan
Write-Host ""

$TrayExe = "C:\ef-map-overlay\build\src\helper\Debug\ef-overlay-tray.exe"

# Check if tray executable exists
if (-not (Test-Path $TrayExe)) {
    Write-Host "x Tray executable not found! Build it first:" -ForegroundColor Red
    Write-Host "  cd c:\ef-map-overlay\build" -ForegroundColor Yellow
    Write-Host "  cmake --build . --config Debug --target ef_overlay_tray" -ForegroundColor Yellow
    exit 1
}

# Check if Store version is running
$StoreHelper = Get-Process -Name "ef-overlay-tray" -ErrorAction SilentlyContinue
if ($StoreHelper) {
    Write-Host "⚠️  Store version is running (PID: $($StoreHelper.Id))" -ForegroundColor Yellow
    Write-Host "   Stopping it to avoid conflicts..." -ForegroundColor Yellow
    Stop-Process -Name "ef-overlay-tray" -Force -ErrorAction SilentlyContinue
    Start-Sleep -Seconds 2
}

Write-Host "Step 1: Launching Debug tray application..." -ForegroundColor Yellow
Write-Host "  This will start the tray icon with your debug features" -ForegroundColor Gray
Write-Host ""

# Launch Debug tray (no console window)
$WorkingDir = Split-Path $TrayExe
$TrayProcess = Start-Process -FilePath $TrayExe -WorkingDirectory $WorkingDir -PassThru

if ($TrayProcess) {
    Write-Host "  + Debug tray launched (PID: $($TrayProcess.Id))" -ForegroundColor Green
} else {
    Write-Host "  x Failed to launch Debug tray" -ForegroundColor Red
    exit 1
}

# Wait for initialization
Write-Host ""
Write-Host "Step 2: Waiting for tray initialization..." -ForegroundColor Yellow
Start-Sleep -Seconds 3

$RunningTray = Get-Process -Name "ef-overlay-tray" -ErrorAction SilentlyContinue | Where-Object { $_.Path -like "*Debug*" }
if ($RunningTray) {
    Write-Host "  + Debug tray is running" -ForegroundColor Green
    Write-Host "    Path: $($RunningTray.Path)" -ForegroundColor Gray
} else {
    Write-Host "  x Debug tray not running" -ForegroundColor Red
    exit 1
}

Write-Host ""
Write-Host "=== MANUAL TEST INSTRUCTIONS ===" -ForegroundColor Cyan
Write-Host ""
Write-Host "The Debug tray is now running. Test:" -ForegroundColor White
Write-Host ""
Write-Host "1. Find 'EF Overlay Helper' icon in system tray (may be in overflow)" -ForegroundColor Yellow
Write-Host ""
Write-Host "2. Right-click the tray icon" -ForegroundColor Yellow
Write-Host "   Expected menu items:" -ForegroundColor Gray
Write-Host "     - Start helper / Stop helper" -ForegroundColor Gray
Write-Host "     - Post sample overlay state" -ForegroundColor Gray
Write-Host "     - Start Overlay" -ForegroundColor Gray
Write-Host "     ---" -ForegroundColor Gray
Write-Host "     - Enable debug logging (checkbox) ← NEW" -ForegroundColor Green
Write-Host "     - Export debug logs... ← NEW" -ForegroundColor Green
Write-Host "     - Open helper logs folder ← NEW" -ForegroundColor Green
Write-Host "     - Open game logs folder ← NEW" -ForegroundColor Green
Write-Host "     ---" -ForegroundColor Gray
Write-Host "     - Copy diagnostics to clipboard" -ForegroundColor Gray
Write-Host "     - Open telemetry history" -ForegroundColor Gray
Write-Host "     - Reset telemetry session" -ForegroundColor Gray
Write-Host "     ---" -ForegroundColor Gray
Write-Host "     - Exit" -ForegroundColor Gray
Write-Host ""
Write-Host "3. Test each new menu item as outlined in earlier instructions" -ForegroundColor Yellow
Write-Host ""
Write-Host "When done testing, press Enter to stop Debug tray and restart Store version..." -ForegroundColor Cyan
Read-Host

# Stop Debug tray
Write-Host ""
Write-Host "Stopping Debug tray..." -ForegroundColor Yellow
Stop-Process -Name "ef-overlay-tray" -Force -ErrorAction SilentlyContinue
Start-Sleep -Seconds 2

# Restart Store version
Write-Host "Restarting Store version..." -ForegroundColor Yellow
Start-Process "shell:AppsFolder\Ef-Map.EF-MapOverlayHelper_r3vrm21jghstm!App" -ErrorAction SilentlyContinue

Write-Host ""
Write-Host "=== TEST COMPLETE ===" -ForegroundColor Cyan
Write-Host ""
Write-Host "Store version should be running again." -ForegroundColor Green
Write-Host "Check system tray for 'EF Overlay Helper' icon." -ForegroundColor Gray
```

---

## Recommendation for Debug Logging Feature

Use **Option 1** (side-by-side):

1. Stop Store helper
2. Build Debug `ef-overlay-tray.exe`
3. Run it directly
4. Test tray menu changes
5. Stop Debug, restart Store version

**Rationale:**
- Debug logging is purely a helper-side feature
- Doesn't involve protocol handling or AppContainer
- Fast iteration needed for testing menu items
- No packaging concerns (just testing C++ behavior)

Save Option 2 (full MSIX) for when you're ready to release this feature to Store.
