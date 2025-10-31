# Debug Logging Feature Smoke Test - Tray Application Version
# Tests actual tray executable (ef-overlay-tray.exe) not console helper
# Updated: 2025-10-31 - Test the REAL tray app with right-click menu

$ErrorActionPreference = "Continue"

Write-Host "=== Debug Logging Feature Smoke Test (Tray Application) ===" -ForegroundColor Cyan
Write-Host ""

$TrayExe = "C:\ef-map-overlay\build\src\helper\Debug\ef-overlay-tray.exe"
$ConfigPath = "$env:LOCALAPPDATA\EFOverlay\config.json"
$LogPath = "$env:LOCALAPPDATA\EFOverlay\logs"

Write-Host "Debug tray executable: $TrayExe" -ForegroundColor Gray
Write-Host ""

# Check if tray executable exists
if (-not (Test-Path $TrayExe)) {
    Write-Host "x Tray executable not found! Build it first:" -ForegroundColor Red
    Write-Host "  cd c:\ef-map-overlay\build" -ForegroundColor Yellow
    Write-Host "  cmake --build . --config Debug --target ef_overlay_tray" -ForegroundColor Yellow
    exit 1
}

Write-Host "  + Debug tray executable found" -ForegroundColor Green
$TrayFileInfo = Get-Item $TrayExe
Write-Host "    Last built: $($TrayFileInfo.LastWriteTime)" -ForegroundColor Gray
Write-Host ""

# Check if Store version is running
$StoreHelper = Get-Process -Name "ef-overlay-tray" -ErrorAction SilentlyContinue
if ($StoreHelper) {
    Write-Host "Step 1: Store version detected running (PID: $($StoreHelper.Id))" -ForegroundColor Yellow
    Write-Host "  Path: $($StoreHelper.Path)" -ForegroundColor Gray
    Write-Host "  Stopping it to avoid conflicts..." -ForegroundColor Yellow
    Stop-Process -Name "ef-overlay-tray" -Force -ErrorAction SilentlyContinue
    Start-Sleep -Seconds 2
    Write-Host "  + Store version stopped" -ForegroundColor Green
} else {
    Write-Host "Step 1: No existing helper running" -ForegroundColor Green
}

Write-Host ""
Write-Host "Step 2: Launching Debug tray application..." -ForegroundColor Yellow
Write-Host "  This will create a tray icon you can right-click" -ForegroundColor Gray
Write-Host ""

# Launch Debug tray (no console window - it's a WIN32 app)
$WorkingDir = Split-Path $TrayExe
$TrayProcess = Start-Process -FilePath $TrayExe -WorkingDirectory $WorkingDir -PassThru -WindowStyle Hidden

if ($TrayProcess) {
    Write-Host "  + Debug tray launched (PID: $($TrayProcess.Id))" -ForegroundColor Green
} else {
    Write-Host "  x Failed to launch Debug tray" -ForegroundColor Red
    exit 1
}

# Wait for initialization
Write-Host ""
Write-Host "Step 3: Waiting for tray initialization..." -ForegroundColor Yellow
Start-Sleep -Seconds 3

$RunningTray = Get-Process -Id $TrayProcess.Id -ErrorAction SilentlyContinue
if ($RunningTray) {
    Write-Host "  + Debug tray is running" -ForegroundColor Green
    Write-Host "    PID: $($RunningTray.Id)" -ForegroundColor Gray
    Write-Host "    Path: $($RunningTray.Path)" -ForegroundColor Gray
} else {
    Write-Host "  x Debug tray crashed or exited" -ForegroundColor Red
    Write-Host "  i Check logs: $LogPath" -ForegroundColor Yellow
    exit 1
}

# Check config file
Write-Host ""
Write-Host "Step 4: Checking config file..." -ForegroundColor Yellow
Write-Host "  Config path: $ConfigPath" -ForegroundColor Gray

if (Test-Path $ConfigPath) {
    $Config = Get-Content $ConfigPath -Raw | ConvertFrom-Json
    Write-Host "  + Config file exists" -ForegroundColor Green
    Write-Host "    debug_logging_enabled: $($Config.debug_logging_enabled)" -ForegroundColor Gray
} else {
    Write-Host "  i Config file not created yet (will be created on first toggle)" -ForegroundColor Yellow
}

# Check log directory
Write-Host ""
Write-Host "Step 5: Checking log directory..." -ForegroundColor Yellow
Write-Host "  Log path: $LogPath" -ForegroundColor Gray

if (Test-Path $LogPath) {
    Write-Host "  + Log directory exists" -ForegroundColor Green
    $LogFiles = Get-ChildItem $LogPath -File -ErrorAction SilentlyContinue
    if ($LogFiles.Count -gt 0) {
        Write-Host "  Log files found: $($LogFiles.Count)" -ForegroundColor Gray
        foreach ($file in $LogFiles | Select-Object -First 3) {
            $SizeKB = [math]::Round($file.Length / 1KB, 2)
            Write-Host "    - $($file.Name) - $SizeKB KB" -ForegroundColor Gray
        }
    }
} else {
    Write-Host "  i Log directory not created yet" -ForegroundColor Yellow
}

# Manual test instructions
Write-Host ""
Write-Host "=== MANUAL TEST INSTRUCTIONS ===" -ForegroundColor Cyan
Write-Host ""
Write-Host "The Debug tray is now running. Look for 'EF Overlay Helper' icon in system tray." -ForegroundColor White
Write-Host "  (May be in the overflow area - click ^ arrow to show hidden icons)" -ForegroundColor Gray
Write-Host ""

Write-Host "1. Right-click the tray icon" -ForegroundColor Yellow
Write-Host "   Expected menu structure:" -ForegroundColor Gray
Write-Host "     Start helper / Stop helper" -ForegroundColor Gray
Write-Host "     Post sample overlay state" -ForegroundColor Gray
Write-Host "     Start Overlay" -ForegroundColor Gray
Write-Host "     ---" -ForegroundColor Gray
Write-Host "     Enable debug logging (checkbox) ← NEW FEATURE" -ForegroundColor Green
Write-Host "     Export debug logs... ← NEW FEATURE" -ForegroundColor Green
Write-Host "     Open helper logs folder ← NEW FEATURE (split menu)" -ForegroundColor Green
Write-Host "     Open game logs folder ← NEW FEATURE (split menu)" -ForegroundColor Green
Write-Host "     ---" -ForegroundColor Gray
Write-Host "     Copy diagnostics to clipboard" -ForegroundColor Gray
Write-Host "     Open telemetry history" -ForegroundColor Gray
Write-Host "     Reset telemetry session" -ForegroundColor Gray
Write-Host "     ---" -ForegroundColor Gray
Write-Host "     Exit" -ForegroundColor Gray
Write-Host ""

Write-Host "2. Click 'Enable debug logging'" -ForegroundColor Yellow
Write-Host "   Expected:" -ForegroundColor Gray
Write-Host "     - Checkmark appears next to menu item" -ForegroundColor Gray
Write-Host "     - Balloon notification: 'Verbose logging enabled'" -ForegroundColor Gray
Write-Host "     - Config file created/updated: $ConfigPath" -ForegroundColor Gray
Write-Host ""

Write-Host "3. Click 'Export debug logs...'" -ForegroundColor Yellow
Write-Host "   Expected:" -ForegroundColor Gray
Write-Host "     - Folder created on Desktop: EFOverlay_Logs_YYYY-MM-DD_HHMMSS" -ForegroundColor Gray
Write-Host "     - Contains: system_info.txt, config.json, sanitized logs" -ForegroundColor Gray
Write-Host "     - Explorer opens automatically showing the folder" -ForegroundColor Gray
Write-Host "     - Balloon notification: 'Debug logs exported to: <path>'" -ForegroundColor Gray
Write-Host ""

Write-Host "4. Click 'Open helper logs folder'" -ForegroundColor Yellow
Write-Host "   Expected:" -ForegroundColor Gray
Write-Host "     - Explorer opens: $LogPath" -ForegroundColor Gray
Write-Host "     - Balloon notification: 'Opened helper logs directory'" -ForegroundColor Gray
Write-Host "     - This should ALWAYS work (helper logs always exist)" -ForegroundColor Gray
Write-Host ""

Write-Host "5. Click 'Open game logs folder'" -ForegroundColor Yellow
Write-Host "   Expected (game NOT running):" -ForegroundColor Gray
Write-Host "     - Balloon warning: 'Game log directory not available...'" -ForegroundColor Gray
Write-Host "     - No Explorer window opens" -ForegroundColor Gray
Write-Host "   Expected (game running):" -ForegroundColor Gray
Write-Host "     - Explorer opens game log directory" -ForegroundColor Gray
Write-Host "     - Balloon notification: 'Opened game logs directory'" -ForegroundColor Gray
Write-Host ""

Write-Host "6. Toggle 'Enable debug logging' again (turn it off)" -ForegroundColor Yellow
Write-Host "   Expected:" -ForegroundColor Gray
Write-Host "     - Checkmark disappears" -ForegroundColor Gray
Write-Host "     - Balloon notification: 'Verbose logging disabled'" -ForegroundColor Gray
Write-Host "     - Config file updated: debug_logging_enabled: false" -ForegroundColor Gray
Write-Host ""

Write-Host "When done testing, press Enter to clean up..." -ForegroundColor Cyan
Read-Host

# Cleanup
Write-Host ""
Write-Host "=== CLEANUP ===" -ForegroundColor Cyan
Write-Host ""

Write-Host "Stopping Debug tray..." -ForegroundColor Yellow
Stop-Process -Name "ef-overlay-tray" -Force -ErrorAction SilentlyContinue
Start-Sleep -Seconds 2

# Check if we should restart Store version
$RestartStore = Read-Host "Restart Store version? (y/n)"
if ($RestartStore -eq "y") {
    Write-Host "Restarting Store version..." -ForegroundColor Yellow
    Start-Process "shell:AppsFolder\Ef-Map.EF-MapOverlayHelper_r3vrm21jghstm!App" -ErrorAction SilentlyContinue
    Start-Sleep -Seconds 2
    
    $StoreRestarted = Get-Process -Name "ef-overlay-tray" -ErrorAction SilentlyContinue
    if ($StoreRestarted) {
        Write-Host "  + Store version restarted (PID: $($StoreRestarted.Id))" -ForegroundColor Green
    } else {
        Write-Host "  i Couldn't auto-restart - launch manually from Start Menu" -ForegroundColor Yellow
    }
}

# Final verification
Write-Host ""
Write-Host "=== FINAL VERIFICATION ===" -ForegroundColor Cyan
Write-Host ""

if (Test-Path $ConfigPath) {
    Write-Host "Config file after testing:" -ForegroundColor Yellow
    Get-Content $ConfigPath | Write-Host -ForegroundColor Gray
    Write-Host ""
}

Write-Host "Desktop export folders (check for EFOverlay_Logs_*):" -ForegroundColor Yellow
$DesktopLogs = Get-ChildItem "$env:USERPROFILE\Desktop\EFOverlay_Logs_*" -Directory -ErrorAction SilentlyContinue
if ($DesktopLogs) {
    foreach ($folder in $DesktopLogs | Select-Object -First 3) {
        Write-Host "  + $($folder.Name)" -ForegroundColor Green
        $Contents = Get-ChildItem $folder.FullName -File
        foreach ($file in $Contents) {
            $SizeKB = [math]::Round($file.Length / 1KB, 2)
            Write-Host "    - $($file.Name) ($SizeKB KB)" -ForegroundColor Gray
        }
    }
} else {
    Write-Host "  i No export folders found on Desktop" -ForegroundColor Yellow
}

Write-Host ""
Write-Host "=== TEST COMPLETE ===" -ForegroundColor Cyan
Write-Host ""
Write-Host "Next steps:" -ForegroundColor White
Write-Host "  - If tests passed: Commit changes to main branch" -ForegroundColor Gray
Write-Host "  - Before Store release: Run full MSIX packaging test (see TESTING_WORKFLOW.md)" -ForegroundColor Gray
