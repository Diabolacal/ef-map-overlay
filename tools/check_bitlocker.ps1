# Check BitLocker and File Encryption Status
# Purpose: Diagnose "file could not be encrypted" errors during injection

Write-Host "`n=== BitLocker & Encryption Check ===" -ForegroundColor Cyan

# Check BitLocker status on system drive
Write-Host "`n[1] BitLocker Status on C: drive:" -ForegroundColor Yellow
$bitlocker = Get-BitLockerVolume -MountPoint "C:" -ErrorAction SilentlyContinue
if ($bitlocker) {
    Write-Host "  Protection Status: $($bitlocker.ProtectionStatus)" -ForegroundColor $(if ($bitlocker.ProtectionStatus -eq 'On') { 'Yellow' } else { 'Green' })
    Write-Host "  Encryption Percentage: $($bitlocker.EncryptionPercentage)%"
    Write-Host "  Volume Status: $($bitlocker.VolumeStatus)"
} else {
    Write-Host "  BitLocker is not enabled on C: drive" -ForegroundColor Green
}

# Check if WindowsApps folder is encrypted
Write-Host "`n[2] WindowsApps Folder Encryption:" -ForegroundColor Yellow
$windowsAppsPath = "C:\Program Files\WindowsApps"
if (Test-Path $windowsAppsPath) {
    $windowsApps = Get-Item $windowsAppsPath -Force
    $encrypted = ($windowsApps.Attributes -band [System.IO.FileAttributes]::Encrypted) -eq [System.IO.FileAttributes]::Encrypted
    Write-Host "  Path: $windowsAppsPath"
    Write-Host "  Encrypted: " -NoNewline
    Write-Host $encrypted -ForegroundColor $(if ($encrypted) { 'Yellow' } else { 'Green' })
} else {
    Write-Host "  WindowsApps folder not found (no Store apps installed?)" -ForegroundColor Gray
}

# Check TEMP folder encryption
Write-Host "`n[3] TEMP Folder Encryption:" -ForegroundColor Yellow
$tempPath = $env:TEMP
$temp = Get-Item $tempPath -ErrorAction SilentlyContinue
if ($temp) {
    $tempEncrypted = ($temp.Attributes -band [System.IO.FileAttributes]::Encrypted) -eq [System.IO.FileAttributes]::Encrypted
    Write-Host "  Path: $tempPath"
    Write-Host "  Encrypted: " -NoNewline
    Write-Host $tempEncrypted -ForegroundColor $(if ($tempEncrypted) { 'Green' } else { 'Yellow' })
} else {
    Write-Host "  ERROR: Cannot access TEMP folder!" -ForegroundColor Red
}

# Check if helper is installed via Store
Write-Host "`n[4] Helper Installation Location:" -ForegroundColor Yellow
$helperProcess = Get-Process -Name "ef-overlay-tray" -ErrorAction SilentlyContinue
if ($helperProcess) {
    $helperPath = $helperProcess.Path
    Write-Host "  Path: $helperPath"
    
    if ($helperPath -match "WindowsApps") {
        Write-Host "  Type: Microsoft Store Package" -ForegroundColor Cyan
        
        # Check if the specific helper executable is encrypted
        $helperFile = Get-Item $helperPath -ErrorAction SilentlyContinue
        if ($helperFile) {
            $helperEncrypted = ($helperFile.Attributes -band [System.IO.FileAttributes]::Encrypted) -eq [System.IO.FileAttributes]::Encrypted
            Write-Host "  Helper EXE Encrypted: " -NoNewline
            Write-Host $helperEncrypted -ForegroundColor $(if ($helperEncrypted) { 'Yellow' } else { 'Green' })
        }
    } else {
        Write-Host "  Type: Sideloaded/Development" -ForegroundColor Green
    }
} else {
    Write-Host "  Helper not currently running" -ForegroundColor Gray
}

# Summary
Write-Host "`n=== DIAGNOSIS ===" -ForegroundColor Cyan
if ($bitlocker -and $bitlocker.ProtectionStatus -eq 'On' -and $encrypted -and -not $tempEncrypted) {
    Write-Host "`n✗ ENCRYPTION MISMATCH DETECTED!" -ForegroundColor Red
    Write-Host "  WindowsApps folder: ENCRYPTED" -ForegroundColor Yellow
    Write-Host "  TEMP folder: NOT ENCRYPTED" -ForegroundColor Yellow
    Write-Host "`n  This causes 'file could not be encrypted' errors!" -ForegroundColor Red
    Write-Host "`n  SOLUTION: Enable encryption on TEMP folder OR disable BitLocker." -ForegroundColor Cyan
} elseif ($bitlocker -and $bitlocker.ProtectionStatus -eq 'On') {
    Write-Host "`nBitLocker is enabled, but encryption settings may be compatible." -ForegroundColor Yellow
} else {
    Write-Host "`nNo BitLocker encryption issues detected." -ForegroundColor Green
}

Write-Host "`n====================================`n"
