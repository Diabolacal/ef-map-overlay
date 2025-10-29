# Build MSIX Package (Unsigned Test Version)
# 
# Purpose: Create unsigned .msix for local testing before signing/distribution
# 
# Prerequisites:
# - Windows SDK installed (provides makeappx.exe)
# - Helper built in Debug or Release config
# 
# Usage:
#   .\build_msix.ps1                    # Uses Debug build
#   .\build_msix.ps1 -Config Release    # Uses Release build
#   .\build_msix.ps1 -Config Debug -OutputDir custom_path
# 
# Output: 
#   Creates ef-overlay-helper_unsigned.msix in specified output directory

param(
    [ValidateSet('Debug', 'Release')]
    [string]$Config = 'Debug',
    
    [string]$OutputDir = (Join-Path $PSScriptRoot '..\..\build\packages')
)

$ErrorActionPreference = 'Stop'

# Resolve paths
$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$helperDir = $scriptDir
$buildDir = Join-Path $scriptDir '..\..\build'
$helperBinDir = Join-Path $buildDir "src\helper\$Config"
$overlayBinDir = Join-Path $buildDir "src\overlay\$Config"
$injectorBinDir = Join-Path $buildDir "src\injector\$Config"

# Validate build artifacts exist
$trayExe = Join-Path $helperBinDir 'ef-overlay-tray.exe'
$overlayDll = Join-Path $overlayBinDir 'ef-overlay.dll'
$injectorExe = Join-Path $injectorBinDir 'ef-overlay-injector.exe'

if (!(Test-Path $trayExe)) {
    Write-Error "Tray executable not found: $trayExe. Build the project first."
}
if (!(Test-Path $overlayDll)) {
    Write-Error "Overlay DLL not found: $overlayDll. Build the project first."
}
if (!(Test-Path $injectorExe)) {
    Write-Error "Injector executable not found: $injectorExe. Build the project first."
}

# Create staging directory
$stagingDir = Join-Path $buildDir 'msix_staging'
if (Test-Path $stagingDir) {
    Remove-Item $stagingDir -Recurse -Force
}
New-Item $stagingDir -ItemType Directory | Out-Null
Write-Host "[1/5] Created staging directory: $stagingDir"

# Copy manifest (rename to AppxManifest.xml for makeappx)
$manifestSrc = Join-Path $helperDir 'Package.appxmanifest'
$manifestDst = Join-Path $stagingDir 'AppxManifest.xml'
Copy-Item $manifestSrc $manifestDst
Write-Host "[2/5] Copied manifest (renamed to AppxManifest.xml)"

# Copy assets
$assetsSrc = Join-Path $helperDir 'Assets'
$assetsDst = Join-Path $stagingDir 'Assets'
Copy-Item $assetsSrc $assetsDst -Recurse
Write-Host "[3/5] Copied assets"

# Copy binaries
Copy-Item $trayExe $stagingDir
Copy-Item $overlayDll $stagingDir
Copy-Item $injectorExe $stagingDir

# Copy dependencies (DLLs from build directory)
$deps = @('asio.dll', 'httplib.dll') # Add any runtime DLLs if needed
foreach ($dep in $deps) {
    $depPath = Join-Path $helperBinDir $dep
    if (Test-Path $depPath) {
        Copy-Item $depPath $stagingDir
        Write-Host "  Copied dependency: $dep"
    }
}

Write-Host "[4/5] Copied binaries and dependencies"

# Find makeappx.exe (Windows SDK)
$makeappxPaths = @(
    'C:\Program Files (x86)\Windows Kits\10\bin\10.0.22621.0\x64\makeappx.exe',
    'C:\Program Files (x86)\Windows Kits\10\bin\10.0.19041.0\x64\makeappx.exe',
    'C:\Program Files (x86)\Windows Kits\10\App Certification Kit\makeappx.exe'
)

$makeappx = $null
foreach ($path in $makeappxPaths) {
    if (Test-Path $path) {
        $makeappx = $path
        break
    }
}

if (!$makeappx) {
    # Try finding via where.exe
    $whereResult = where.exe makeappx.exe 2>$null | Select-Object -First 1
    if ($whereResult) {
        $makeappx = $whereResult
    }
}

if (!$makeappx) {
    Write-Error @"
makeappx.exe not found. Install Windows SDK:
https://developer.microsoft.com/en-us/windows/downloads/windows-sdk/

Searched locations:
$($makeappxPaths -join "`n")
"@
}

Write-Host "  Using makeappx.exe: $makeappx"

# Create output directory
if (!(Test-Path $OutputDir)) {
    New-Item $OutputDir -ItemType Directory | Out-Null
}

# Build package
$msixPath = Join-Path $OutputDir "ef-overlay-helper_unsigned_$Config.msix"
if (Test-Path $msixPath) {
    Remove-Item $msixPath -Force
}

Write-Host "[5/5] Building MSIX package..."
& $makeappx pack /d $stagingDir /p $msixPath /nv

if ($LASTEXITCODE -ne 0) {
    Write-Error "makeappx.exe failed with exit code $LASTEXITCODE"
}

Write-Host "`n✅ MSIX package created: $msixPath" -ForegroundColor Green
Write-Host "`nNext steps:"
Write-Host "1. Enable Developer Mode in Windows Settings"
Write-Host "2. Right-click the .msix file → Install"
Write-Host "3. Test protocol handler: Start menu → EF Map Overlay Helper"
Write-Host "4. Verify startup task: Settings → Apps → Startup"
Write-Host "`nNote: This is an UNSIGNED package for local testing only."
Write-Host "      For distribution, sign with Azure Code Signing certificate."

# Cleanup staging
Remove-Item $stagingDir -Recurse -Force
Write-Host "`nCleaned up staging directory."
