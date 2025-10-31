# Debug Logging Feature Smoke Test (v2 - Split menu items)
# Tests Phase 1 (menu toggle) + Phase 3 (export/sanitization)
# Updated: 2025-10-31 - Split game vs helper logs menu items

$ErrorActionPreference = "Continue"

Write-Host "=== Debug Logging Feature Smoke Test v2 ===" -ForegroundColor Cyan
Write-Host ""

$HelperExe = "C:\ef-map-overlay\build\src\helper\Debug\ef-overlay-helper.exe"
$ConfigPath = "$env:LOCALAPPDATA\EFOverlay\config.json"
$LogPath = "$env:LOCALAPPDATA\EFOverlay\logs"

Write-Host "Helper executable: $HelperExe" -ForegroundColor Gray
Write-Host ""

# Check if helper exists
if (-not (Test-Path $HelperExe)) {
    Write-Host "x Helper not found! Build it first:" -ForegroundColor Red
    Write-Host "  cd c:\ef-map-overlay\build" -ForegroundColor Yellow
    Write-Host "  cmake --build . --config Debug --target ef_overlay_helper" -ForegroundColor Yellow
    exit 1
}

# Launch helper in external PowerShell window (REQUIRED - VS Code terminal breaks binding)
Write-Host "Step 1: Launching helper in EXTERNAL PowerShell window..." -ForegroundColor Yellow
Write-Host "  This is REQUIRED - VS Code integrated terminal breaks overlay binding" -ForegroundColor Gray
Write-Host ""

$WorkingDir = Split-Path $HelperExe
$HelperProcess = Start-Process -FilePath $HelperExe -WorkingDirectory $WorkingDir -PassThru

if ($HelperProcess) {
    Write-Host "  + Helper launched (PID: $($HelperProcess.Id))" -ForegroundColor Green
} else {
    Write-Host "  x Failed to launch helper" -ForegroundColor Red
    exit 1
}

# Wait for initialization
Write-Host ""
Write-Host "Step 2: Waiting for helper initialization..." -ForegroundColor Yellow
Start-Sleep -Seconds 2

$RunningHelper = Get-Process -Name "ef-overlay-helper" -ErrorAction SilentlyContinue
if ($RunningHelper) {
    Write-Host "  + Helper is running" -ForegroundColor Green
} else {
    Write-Host "  x Helper not running" -ForegroundColor Red
    exit 1
}

# Check config file
Write-Host ""
Write-Host "Step 3: Checking config file..." -ForegroundColor Yellow
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
Write-Host "Step 4: Checking log directory..." -ForegroundColor Yellow
Write-Host "  Log path: $LogPath" -ForegroundColor Gray

if (Test-Path $LogPath) {
    Write-Host "  + Log directory exists" -ForegroundColor Green
    $LogFiles = Get-ChildItem $LogPath -File -ErrorAction SilentlyContinue
    Write-Host "  Log files found: $($LogFiles.Count)" -ForegroundColor Gray
    foreach ($file in $LogFiles) {
        $SizeKB = [math]::Round($file.Length / 1KB, 2)
        Write-Host "    - $($file.Name) - $SizeKB KB" -ForegroundColor Gray
    }
} else {
    Write-Host "  i Log directory not created yet" -ForegroundColor Yellow
}

# Manual test instructions
Write-Host ""
Write-Host "=== MANUAL TEST INSTRUCTIONS ===" -ForegroundColor Cyan
Write-Host ""
Write-Host "The helper is now running with a tray icon. Please test:" -ForegroundColor White
Write-Host ""
Write-Host "1. Right-click the tray icon (look for 'EF Overlay Helper' in system tray)" -ForegroundColor Yellow
Write-Host "   Expected menu items (updated in v2):" -ForegroundColor Gray
Write-Host "     - Enable debug logging (checkbox)" -ForegroundColor Gray
Write-Host "     - Export debug logs..." -ForegroundColor Gray
Write-Host "     - Open helper logs folder  (NEW - always opens helper diagnostics)" -ForegroundColor Green
Write-Host "     - Open game logs folder    (NEW - opens game logs or shows warning)" -ForegroundColor Green
Write-Host ""

Write-Host "2. Click 'Enable debug logging'" -ForegroundColor Yellow
Write-Host "   Expected: Checkmark appears, balloon notification 'Verbose logging enabled'" -ForegroundColor Gray
Write-Host "   Verify: Config file should update with debug_logging_enabled: true" -ForegroundColor Gray
Write-Host ""

Write-Host "3. Click 'Export debug logs...'" -ForegroundColor Yellow
Write-Host "   Expected: Folder created on Desktop: EFOverlay_Logs_YYYY-MM-DD_HHMMSS" -ForegroundColor Gray
Write-Host "   Contains:" -ForegroundColor Gray
Write-Host "     - system_info.txt (with process info, elevation, session IDs)" -ForegroundColor Gray
Write-Host "     - config.json" -ForegroundColor Gray
Write-Host "     - Any log files (sanitized)" -ForegroundColor Gray
Write-Host "   Explorer should open automatically showing the folder" -ForegroundColor Gray
Write-Host ""

Write-Host "4. Click 'Open helper logs folder'" -ForegroundColor Yellow
Write-Host "   Expected: Explorer opens: $LogPath" -ForegroundColor Gray
Write-Host ""

Write-Host "5. Click 'Open game logs folder'" -ForegroundColor Yellow
Write-Host "   Expected (game NOT running): Balloon warning 'Game log directory not available...'" -ForegroundColor Gray
Write-Host "   Expected (game running): Explorer opens game log directory" -ForegroundColor Gray
Write-Host ""

Write-Host "6. Toggle 'Enable debug logging' again (turn it off)" -ForegroundColor Yellow
Write-Host "   Expected: Checkmark disappears, balloon 'Verbose logging disabled'" -ForegroundColor Gray
Write-Host ""

Write-Host "After testing, press Enter to stop the helper and check results..." -ForegroundColor Cyan
Read-Host

# Stop helper
Write-Host ""
Write-Host "Stopping helper..." -ForegroundColor Yellow
Stop-Process -Name "ef-overlay-helper" -Force -ErrorAction SilentlyContinue
Start-Sleep -Seconds 1

# Final verification
Write-Host ""
Write-Host "=== FINAL VERIFICATION ===" -ForegroundColor Cyan
Write-Host ""

if (Test-Path $ConfigPath) {
    Write-Host "Config file after testing:" -ForegroundColor Yellow
    Get-Content $ConfigPath
    Write-Host ""
}

Write-Host "Desktop folders (check for EFOverlay_Logs_*):" -ForegroundColor Yellow
$DesktopLogs = Get-ChildItem "$env:USERPROFILE\Desktop\EFOverlay_Logs_*" -Directory -ErrorAction SilentlyContinue
if ($DesktopLogs) {
    foreach ($folder in $DesktopLogs) {
        Write-Host "  + $($folder.Name)" -ForegroundColor Green
        $Contents = Get-ChildItem $folder.FullName -File
        foreach ($file in $Contents) {
            Write-Host "    - $($file.Name)" -ForegroundColor Gray
        }
    }
} else {
    Write-Host "  i No export folders found on Desktop" -ForegroundColor Yellow
}

Write-Host ""
Write-Host "=== TEST COMPLETE ===" -ForegroundColor Cyan
