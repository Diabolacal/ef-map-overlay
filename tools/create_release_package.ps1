# EF Map Overlay Helper - Release Package Creator
# Creates a distributable ZIP for GitHub Releases

param(
    [string]$Version = "1.0.0",
    [string]$BuildConfig = "Release"
)

$ErrorActionPreference = "Stop"

Write-Host "Creating EF Map Helper v$Version release package..." -ForegroundColor Cyan

# Paths
$buildDir = "C:\ef-map-overlay\build\src"
$outputDir = "C:\ef-map-overlay\releases"
$packageDir = "$outputDir\EFMapHelper-v$Version"
$zipPath = "$outputDir\EFMapHelper-v$Version.zip"

# Create output directory
if (!(Test-Path $outputDir)) {
    New-Item -ItemType Directory -Path $outputDir | Out-Null
}

# Clean existing package directory
if (Test-Path $packageDir) {
    Remove-Item -Path $packageDir -Recurse -Force
}

New-Item -ItemType Directory -Path $packageDir | Out-Null

Write-Host "Copying binaries..." -ForegroundColor Yellow

# Copy binaries
$binaries = @(
    "$buildDir\helper\$BuildConfig\ef-overlay-helper.exe",
    "$buildDir\overlay\$BuildConfig\ef-overlay.dll",
    "$buildDir\injector\$BuildConfig\ef-overlay-injector.exe"
)

foreach ($binary in $binaries) {
    if (!(Test-Path $binary)) {
        Write-Error "Binary not found: $binary"
        exit 1
    }
    Copy-Item -Path $binary -Destination $packageDir
    Write-Host "  ✓ $(Split-Path $binary -Leaf)" -ForegroundColor Green
}

Write-Host "`nCreating launcher script..." -ForegroundColor Yellow

# Create PowerShell launcher
$launcherContent = @'
# EF Map Overlay Helper Launcher
# This script launches the helper with proper execution policy

$ErrorActionPreference = "Stop"

Write-Host "EF Map Overlay Helper - Launcher" -ForegroundColor Cyan
Write-Host "=================================" -ForegroundColor Cyan
Write-Host ""

# Get helper path
$helperPath = Join-Path $PSScriptRoot "ef-overlay-helper.exe"

# Check if helper exists
if (!(Test-Path $helperPath)) {
    Write-Host "ERROR: ef-overlay-helper.exe not found!" -ForegroundColor Red
    Write-Host "Expected location: $helperPath" -ForegroundColor Yellow
    Read-Host "Press Enter to exit"
    exit 1
}

Write-Host "Starting EF Map Helper..." -ForegroundColor Green
Write-Host "The helper will appear in your system tray (look for orange EF icon)" -ForegroundColor Yellow
Write-Host ""

try {
    Start-Process -FilePath $helperPath -WorkingDirectory $PSScriptRoot
    Write-Host "✓ Helper started successfully!" -ForegroundColor Green
    Write-Host ""
    Write-Host "Next steps:" -ForegroundColor Cyan
    Write-Host "1. Open EVE Frontier" -ForegroundColor White
    Write-Host "2. Visit ef-map.com in your browser" -ForegroundColor White
    Write-Host "3. Calculate a route" -ForegroundColor White
    Write-Host "4. Press F8 in-game to toggle overlay" -ForegroundColor White
    Write-Host ""
}
catch {
    Write-Host "ERROR: Failed to start helper!" -ForegroundColor Red
    Write-Host $_.Exception.Message -ForegroundColor Yellow
    Read-Host "Press Enter to exit"
    exit 1
}

Start-Sleep -Seconds 3
'@

$launcherContent | Out-File -FilePath "$packageDir\launch_helper.ps1" -Encoding UTF8
Write-Host "  ✓ launch_helper.ps1" -ForegroundColor Green

Write-Host "`nCreating README..." -ForegroundColor Yellow

# Create README
$readmeContent = @'
EF Map Overlay Helper v{VERSION}
================================

WHAT IS THIS?
-------------
An in-game overlay for EVE Frontier that displays routing information from ef-map.com.
Press F8 in-game to toggle the overlay on/off.

FEATURES
--------
- Route navigation with auto-advance
- Current system tracking (follow mode)
- Mining & combat telemetry
- Proximity scanner (P-SCAN)
- Bookmark creation
- Visit tracking

INSTALLATION INSTRUCTIONS
--------------------------
IMPORTANT: Windows Defender may flag this as suspicious due to process injection.
This is expected - the tool hooks into EVE Frontier to display the overlay.

1. RIGHT-CLICK the ZIP file → Properties → Check "Unblock" → Click OK
   (This is CRITICAL - Windows marks downloads from internet as blocked)

2. Extract ALL files to a permanent location:
   Example: C:\EFMapHelper\
   DO NOT run from Downloads folder or Temp

3. BEFORE RUNNING: Add Windows Defender exclusion (REQUIRED)
   a. Open Windows Security (search in Start Menu)
   b. Virus & threat protection → Manage settings
   c. Scroll down to "Exclusions"
   d. Click "Add or remove exclusions"
   e. Add an exclusion → Folder
   f. Browse to your installation folder (e.g., C:\EFMapHelper\)
   g. Click "Select Folder"

4. Launch the helper:
   METHOD A (Recommended): Right-click "launch_helper.ps1" → Run with PowerShell
   METHOD B: Double-click "ef-overlay-helper.exe" directly

5. Helper will appear in system tray (orange EF icon)

6. Open EVE Frontier and visit ef-map.com

7. Press F8 in-game to toggle overlay

TROUBLESHOOTING
----------------
Q: PowerShell won't run the script
A: Open PowerShell as Admin and run:
   Set-ExecutionPolicy -Scope CurrentUser RemoteSigned

Q: Windows Defender deleted the EXE
A: You MUST add the folder exclusion BEFORE extracting the ZIP
   If already deleted, restore from Defender quarantine:
   Windows Security → Virus & threat protection → Protection history
   Find the quarantined file → Restore → Add exclusion

Q: Overlay doesn't appear in-game
A: 
   1. Check system tray - is helper running?
   2. Visit ef-map.com and calculate a route
   3. Press F8 in EVE Frontier (windowed or fullscreen both work)
   4. Check helper logs in: %LOCALAPPDATA%\EFOverlay\logs\

Q: "This app can't run on your PC" error
A: You downloaded the wrong architecture. This is for 64-bit Windows only.

PRIVACY & SECURITY
------------------
- All data stays on your PC (no cloud uploads)
- Telemetry data never leaves your machine
- Open source: https://github.com/Diabolacal/EF-Map
- You can audit the code yourself

SYSTEM REQUIREMENTS
-------------------
- Windows 10/11 (64-bit)
- EVE Frontier installed
- DirectX 12 compatible GPU
- Modern web browser

UNINSTALL
---------
1. Close helper (right-click tray icon → Quit)
2. Delete installation folder
3. Remove Windows Defender exclusion
4. Delete data folder: %LOCALAPPDATA%\EFOverlay\

SUPPORT
-------
- GitHub Issues: https://github.com/Diabolacal/EF-Map/issues
- Website: https://ef-map.com
- Discord: [Your Discord invite if you have one]

VERSION HISTORY
---------------
v$Version - Initial release
  - Route navigation
  - Follow mode
  - Mining/combat telemetry
  - P-SCAN
  - Bookmark creation
  - Visit tracking

LICENSE
-------
[Your license - suggest MIT or similar]

DISCLAIMER
----------
This is a third-party tool not affiliated with EVE Frontier developers.
Use at your own risk. The authors are not responsible for any game bans
or technical issues (though none are expected - EVE Frontier has no anti-cheat).
'@

$readmeContent = $readmeContent -replace '{VERSION}', $Version
$readmeContent | Out-File -FilePath "$packageDir\README.txt" -Encoding UTF8
Write-Host "  ✓ README.txt" -ForegroundColor Green

Write-Host "`nCreating ZIP archive..." -ForegroundColor Yellow

# Remove old ZIP if exists
if (Test-Path $zipPath) {
    Remove-Item -Path $zipPath -Force
}

# Create ZIP
Compress-Archive -Path "$packageDir\*" -DestinationPath $zipPath -CompressionLevel Optimal

Write-Host "  ✓ $zipPath" -ForegroundColor Green

# Clean up package directory
Remove-Item -Path $packageDir -Recurse -Force

Write-Host "`n=================================" -ForegroundColor Cyan
Write-Host "Release package created successfully!" -ForegroundColor Green
Write-Host "=================================" -ForegroundColor Cyan
Write-Host ""
Write-Host "Package location: $zipPath" -ForegroundColor Yellow
Write-Host "Package size: $([math]::Round((Get-Item $zipPath).Length / 1MB, 2)) MB" -ForegroundColor Yellow
Write-Host ""
Write-Host "Next steps:" -ForegroundColor Cyan
Write-Host "1. Create GitHub Release: https://github.com/Diabolacal/EF-Map/releases/new" -ForegroundColor White
Write-Host "2. Tag version: v$Version" -ForegroundColor White
Write-Host "3. Upload $zipPath as release asset" -ForegroundColor White
Write-Host "4. Publish release" -ForegroundColor White
Write-Host ""
Write-Host "Users will download from:" -ForegroundColor Cyan
Write-Host "https://github.com/Diabolacal/EF-Map/releases/download/v$Version/EFMapHelper-v$Version.zip" -ForegroundColor Yellow
