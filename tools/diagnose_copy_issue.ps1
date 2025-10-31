# Diagnose file could not be encrypted copy errors
Write-Host ""
Write-Host "=== File Copy Encryption Diagnostic ===" -ForegroundColor Cyan

$helper = Get-Process -Name "ef-overlay-tray" -ErrorAction SilentlyContinue
if (-not $helper) {
    Write-Host "ERROR: ef-overlay-tray not running" -ForegroundColor Red
    exit 1
}

$helperPath = $helper.Path
$helperDir = Split-Path $helperPath
$injectorPath = Join-Path $helperDir "ef-overlay-injector.exe"
$dllPath = Join-Path $helperDir "ef-overlay.dll"

Write-Host ""
Write-Host "[1] Helper: $helperPath" -ForegroundColor Yellow
Write-Host ""
Write-Host "[2] Source Files:" -ForegroundColor Yellow
foreach ($file in @($injectorPath, $dllPath)) {
    if (Test-Path $file) {
        $item = Get-Item $file -Force
        $enc = ($item.Attributes -band 16384) -ne 0
        Write-Host "  $($item.Name): Exists=$true, Encrypted=$enc, Size=$($item.Length)"
    } else {
        Write-Host "  $(Split-Path $file -Leaf): MISSING!" -ForegroundColor Red
    }
}

Write-Host ""
Write-Host "[3] TEMP Folder:" -ForegroundColor Yellow
$tempPath = $env:TEMP
$tempItem = Get-Item $tempPath -Force
$tempEnc = ($tempItem.Attributes -band 16384) -ne 0
Write-Host "  Path: $tempPath"
Write-Host "  Encrypted: $tempEnc"

Write-Host ""
Write-Host "[4] Auto-Encrypt Test:" -ForegroundColor Yellow
$testFile = Join-Path $tempPath "ef-test-$(Get-Random).tmp"
"test" | Out-File $testFile -Force
$testItem = Get-Item $testFile -Force
$autoEnc = ($testItem.Attributes -band 16384) -ne 0
Write-Host "  New files auto-encrypt: $autoEnc"
Remove-Item $testFile -Force

Write-Host ""
Write-Host "=== DIAGNOSIS ===" -ForegroundColor Cyan
if ($autoEnc) {
    Write-Host "ISSUE: TEMP folder has EFS auto-encryption policy!" -ForegroundColor Red
    Write-Host "SOLUTION: Contact IT to disable EFS on TEMP folder" -ForegroundColor Cyan
} else {
    Write-Host "No encryption conflicts detected" -ForegroundColor Green
}
Write-Host ""
