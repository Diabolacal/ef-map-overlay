# Debug Logging Feature Smoke Test
# Tests the new debug logging menu items and export functionality

Write-Host "=== Debug Logging Feature Smoke Test ===" -ForegroundColor Cyan
Write-Host ""

# Check if helper is already running
$existingHelper = Get-Process -Name "ef-overlay-helper" -ErrorAction SilentlyContinue
if ($existingHelper) {
    Write-Host "WARNING: Helper is already running - PID: $($existingHelper.Id)" -ForegroundColor Yellow
    Write-Host "Stopping existing instance..." -ForegroundColor Yellow
    Stop-Process -Id $existingHelper.Id -Force
    Start-Sleep -Seconds 2
}

# Path to helper executable
$helperPath = "C:\ef-map-overlay\build\src\helper\Debug\ef-overlay-helper.exe"
$helperDir = Split-Path $helperPath

if (!(Test-Path $helperPath)) {
    Write-Host "ERROR: Helper not found at: $helperPath" -ForegroundColor Red
    exit 1
}

Write-Host "Helper executable: $helperPath" -ForegroundColor Green
Write-Host ""

# Launch helper in external PowerShell window (CRITICAL: must be external, not VS Code terminal)
Write-Host "Step 1: Launching helper in EXTERNAL PowerShell window..." -ForegroundColor Cyan
Write-Host "  This is REQUIRED - VS Code integrated terminal breaks overlay binding" -ForegroundColor Yellow
Write-Host ""

try {
    $helperProcess = Start-Process -FilePath $helperPath `
                                   -WorkingDirectory $helperDir `
                                   -PassThru `
                                   -WindowStyle Hidden
    
    if ($helperProcess) {
        Write-Host "  + Helper launched (PID: $($helperProcess.Id))" -ForegroundColor Green
    } else {
        Write-Host "  x Failed to launch helper" -ForegroundColor Red
        exit 1
    }
} catch {
    Write-Host "  x Exception launching helper: $_" -ForegroundColor Red
    exit 1
}

# Wait for helper to initialize
Write-Host ""
Write-Host "Step 2: Waiting for helper initialization..." -ForegroundColor Cyan
Start-Sleep -Seconds 3

# Check if helper is still running
$helperRunning = Get-Process -Id $helperProcess.Id -ErrorAction SilentlyContinue
if (!$helperRunning) {
    Write-Host "  x Helper process died after launch" -ForegroundColor Red
    exit 1
}
Write-Host "  + Helper is running" -ForegroundColor Green

# Check config file location
$configPath = Join-Path $env:LOCALAPPDATA "EFOverlay\config.json"
Write-Host ""
Write-Host "Step 3: Checking config file..." -ForegroundColor Cyan
Write-Host "  Config path: $configPath" -ForegroundColor Gray

if (Test-Path $configPath) {
    Write-Host "  + Config file exists" -ForegroundColor Green
    $configContent = Get-Content $configPath -Raw
    Write-Host "  Content:" -ForegroundColor Gray
    Write-Host $configContent -ForegroundColor Gray
} else {
    Write-Host "  i Config file not created yet (will be created on first toggle)" -ForegroundColor Yellow
}

# Check log directory
$logDir = Join-Path $env:LOCALAPPDATA "EFOverlay\logs"
Write-Host ""
Write-Host "Step 4: Checking log directory..." -ForegroundColor Cyan
Write-Host "  Log path: $logDir" -ForegroundColor Gray

if (Test-Path $logDir) {
    Write-Host "  + Log directory exists" -ForegroundColor Green
    $logFiles = Get-ChildItem $logDir -File -ErrorAction SilentlyContinue
    if ($logFiles) {
        Write-Host "  Log files found: $($logFiles.Count)" -ForegroundColor Green
        foreach ($file in $logFiles | Select-Object -First 3) {
            $sizeKB = [math]::Round($file.Length/1024, 2)
            Write-Host "    - $($file.Name) - $sizeKB KB" -ForegroundColor Gray
        }
    } else {
        Write-Host "  i No log files yet" -ForegroundColor Yellow
    }
} else {
    Write-Host "  i Log directory not created yet" -ForegroundColor Yellow
}

# Manual test instructions
Write-Host ""
Write-Host "=== MANUAL TEST INSTRUCTIONS ===" -ForegroundColor Cyan
Write-Host ""
Write-Host "The helper is now running with a tray icon. Please test:" -ForegroundColor Yellow
Write-Host ""
Write-Host "1. Right-click the tray icon (look for 'EF Overlay Helper' in system tray)" -ForegroundColor White
Write-Host "   Expected menu items (new ones):" -ForegroundColor Gray
Write-Host "     - Enable debug logging (checkbox)" -ForegroundColor Gray
Write-Host "     - Export debug logs..." -ForegroundColor Gray
Write-Host "     - Open logs folder" -ForegroundColor Gray
Write-Host ""
Write-Host "2. Click 'Enable debug logging'" -ForegroundColor White
Write-Host "   Expected: Checkmark appears, balloon notification 'Verbose logging enabled'" -ForegroundColor Gray
Write-Host "   Verify: Config file should update with debug_logging_enabled: true" -ForegroundColor Gray
Write-Host ""
Write-Host "3. Click 'Export debug logs...'" -ForegroundColor White
Write-Host "   Expected: Folder created on Desktop: EFOverlay_Logs_YYYY-MM-DD_HHMMSS" -ForegroundColor Gray
Write-Host "   Contains:" -ForegroundColor Gray
Write-Host "     - system_info.txt (with process info, elevation, session IDs)" -ForegroundColor Gray
Write-Host "     - config.json" -ForegroundColor Gray
Write-Host "     - Any log files (sanitized)" -ForegroundColor Gray
Write-Host "   Explorer should open automatically showing the folder" -ForegroundColor Gray
Write-Host ""
Write-Host "4. Click 'Open logs folder'" -ForegroundColor White
Write-Host "   Expected: Explorer opens: $logDir" -ForegroundColor Gray
Write-Host ""
Write-Host "5. Toggle 'Enable debug logging' again (turn it off)" -ForegroundColor White
Write-Host "   Expected: Checkmark disappears, balloon 'Verbose logging disabled'" -ForegroundColor Gray
Write-Host ""
Write-Host "After testing, press Enter to stop the helper and check results..." -ForegroundColor Cyan
$null = Read-Host

# Stop helper
Write-Host ""
Write-Host "Stopping helper..." -ForegroundColor Cyan
Stop-Process -Id $helperProcess.Id -Force -ErrorAction SilentlyContinue
Start-Sleep -Seconds 1

# Verify config was updated
Write-Host ""
Write-Host "=== VERIFICATION ===" -ForegroundColor Cyan
Write-Host ""

if (Test-Path $configPath) {
    Write-Host "+ Config file exists: $configPath" -ForegroundColor Green
    $configContent = Get-Content $configPath -Raw
    Write-Host "Content:" -ForegroundColor Gray
    Write-Host $configContent
    
    if ($configContent -match '"debug_logging_enabled"') {
        Write-Host "  + debug_logging_enabled field found" -ForegroundColor Green
    } else {
        Write-Host "  x debug_logging_enabled field MISSING" -ForegroundColor Red
    }
} else {
    Write-Host "x Config file NOT created" -ForegroundColor Red
}

# Check for exported logs on Desktop
$desktop = [Environment]::GetFolderPath("Desktop")
$exportFolders = Get-ChildItem $desktop -Directory -Filter "EFOverlay_Logs_*" -ErrorAction SilentlyContinue

Write-Host ""
if ($exportFolders) {
    Write-Host "+ Found $($exportFolders.Count) exported log folders on Desktop:" -ForegroundColor Green
    foreach ($folder in $exportFolders) {
        Write-Host "  - $($folder.Name)" -ForegroundColor Gray
        $sysInfo = Join-Path $folder.FullName "system_info.txt"
        if (Test-Path $sysInfo) {
            Write-Host "    + system_info.txt exists" -ForegroundColor Green
            $sysInfoContent = Get-Content $sysInfo -Raw
            if ($sysInfoContent -match "Elevation mismatch" -or $sysInfoContent -match "Session mismatch") {
                Write-Host "    ! Found elevation/session warnings (expected if game not running)" -ForegroundColor Yellow
            }
        } else {
            Write-Host "    x system_info.txt MISSING" -ForegroundColor Red
        }
    }
} else {
    Write-Host "i No exported log folders found on Desktop (user may not have exported)" -ForegroundColor Yellow
}

Write-Host ""
Write-Host "=== TEST COMPLETE ===" -ForegroundColor Cyan
Write-Host ""
Write-Host "Summary:" -ForegroundColor White
Write-Host "  - Helper launched successfully: +" -ForegroundColor Green
Write-Host "  - Config persistence: " -NoNewline
if (Test-Path $configPath) {
    Write-Host "+" -ForegroundColor Green
} else {
    Write-Host "x (not tested)" -ForegroundColor Yellow
}
Write-Host "  - Export functionality: " -NoNewline
if ($exportFolders) {
    Write-Host "+" -ForegroundColor Green
} else {
    Write-Host "? (not tested or user didn't export)" -ForegroundColor Yellow
}
Write-Host ""
