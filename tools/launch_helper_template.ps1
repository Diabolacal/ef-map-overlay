# EF Map Overlay Helper Launcher

$ErrorActionPreference = "Stop"

Write-Host "EF Map Overlay Helper - Launcher" -ForegroundColor Cyan
Write-Host ""

$helperPath = Join-Path $PSScriptRoot "ef-overlay-helper.exe"

if (!(Test-Path $helperPath)) {
    Write-Host "ERROR: ef-overlay-helper.exe not found!" -ForegroundColor Red
    Read-Host "Press Enter to exit"
    exit 1
}

Write-Host "Starting EF Map Helper..." -ForegroundColor Green
Write-Host "Look for the orange EF icon in your system tray" -ForegroundColor Yellow
Write-Host ""

try {
    Start-Process -FilePath $helperPath -WorkingDirectory $PSScriptRoot
    Write-Host "Helper started!" -ForegroundColor Green
    Write-Host ""
    Write-Host "Next steps:" -ForegroundColor Cyan
    Write-Host "1. Open EVE Frontier"
    Write-Host "2. Visit ef-map.com and calculate a route"
    Write-Host "3. Press F8 in-game to toggle overlay"
    Write-Host ""
    Start-Sleep -Seconds 3
}
catch {
    Write-Host "ERROR: Failed to start helper!" -ForegroundColor Red
    Write-Host $_.Exception.Message
    Read-Host "Press Enter to exit"
    exit 1
}
