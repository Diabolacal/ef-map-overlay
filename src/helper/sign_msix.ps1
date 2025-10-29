# Sign MSIX Package with Test Certificate
# 
# Purpose: Sign unsigned MSIX with local test certificate for installation
# 
# Prerequisites:
#   - Run create_test_cert.ps1 first to generate certificate
#   - Build unsigned MSIX via build_msix.ps1
# 
# Usage:
#   .\sign_msix.ps1                    # Signs Debug build
#   .\sign_msix.ps1 -Config Release    # Signs Release build

param(
    [ValidateSet('Debug', 'Release')]
    [string]$Config = 'Debug'
)

$ErrorActionPreference = 'Stop'

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$buildDir = Join-Path $scriptDir '..\..\build'
$packagesDir = Join-Path $buildDir 'packages'

$unsignedPath = Join-Path $packagesDir "ef-overlay-helper_unsigned_$Config.msix"
$signedPath = Join-Path $packagesDir "ef-overlay-helper_signed_$Config.msix"
$certPath = Join-Path $scriptDir 'ef-overlay-test-cert.pfx'

# Validate inputs
if (!(Test-Path $unsignedPath)) {
    Write-Error "Unsigned MSIX not found: $unsignedPath`nRun .\build_msix.ps1 first"
}

if (!(Test-Path $certPath)) {
    Write-Error "Certificate not found: $certPath`nRun .\create_test_cert.ps1 first"
}

# Find signtool.exe (Windows SDK)
$signtoolPaths = @(
    'C:\Program Files (x86)\Windows Kits\10\bin\10.0.22621.0\x64\signtool.exe',
    'C:\Program Files (x86)\Windows Kits\10\bin\10.0.19041.0\x64\signtool.exe',
    'C:\Program Files (x86)\Windows Kits\10\App Certification Kit\signtool.exe'
)

$signtool = $null
foreach ($path in $signtoolPaths) {
    if (Test-Path $path) {
        $signtool = $path
        break
    }
}

if (!$signtool) {
    # Try finding via where.exe
    $whereResult = where.exe signtool.exe 2>$null | Select-Object -First 1
    if ($whereResult) {
        $signtool = $whereResult
    }
}

if (!$signtool) {
    Write-Error @"
signtool.exe not found. Install Windows SDK:
https://developer.microsoft.com/en-us/windows/downloads/windows-sdk/

Searched locations:
$($signtoolPaths -join "`n")
"@
}

Write-Host "Using signtool.exe: $signtool"

# Copy unsigned to signed path
Copy-Item $unsignedPath $signedPath -Force
Write-Host "[1/2] Copied unsigned MSIX to: $signedPath"

# Sign with test certificate
Write-Host "[2/2] Signing MSIX..."
& $signtool sign /fd SHA256 /a /f $certPath /p "test123" $signedPath

if ($LASTEXITCODE -ne 0) {
    Write-Error "signtool.exe failed with exit code $LASTEXITCODE"
}

Write-Host "`n✅ MSIX signed successfully: $signedPath" -ForegroundColor Green
Write-Host "`nNext steps:"
Write-Host "1. Right-click the signed .msix file → Install"
Write-Host "2. Installation should succeed without warnings"
Write-Host "3. Launch from Start menu: 'EF Map Overlay Helper'"
Write-Host "4. Test protocol: Navigate to ef-overlay://test in browser"
