# MSIX Package Verification Script
# MANDATORY: Run this before every Store upload
# Purpose: Prevent incomplete packages from being uploaded

param(
    [string]$MsixPath = "",
    [switch]$ExtractOnly
)

$ErrorActionPreference = "Stop"

if ($MsixPath -eq "") {
    $RepoRoot = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
    $ReleasesDir = Join-Path $RepoRoot "releases"
    $LatestMsix = Get-ChildItem $ReleasesDir -Filter "*.msix" | Sort-Object LastWriteTime -Descending | Select-Object -First 1
    if ($null -eq $LatestMsix) {
        Write-Host "ERROR: No MSIX packages found in $ReleasesDir" -ForegroundColor Red
        Write-Host "Specify -MsixPath explicitly or build a package first" -ForegroundColor Yellow
        exit 1
    }
    $MsixPath = $LatestMsix.FullName
    Write-Host "Auto-detected latest MSIX: $MsixPath" -ForegroundColor Cyan
}

if (!(Test-Path $MsixPath)) {
    Write-Host "ERROR: MSIX not found: $MsixPath" -ForegroundColor Red
    exit 1
}

$MsixName = [System.IO.Path]::GetFileNameWithoutExtension($MsixPath)
$ExtractDir = Join-Path $PSScriptRoot "extracted_$MsixName"

Write-Host ""
Write-Host "==================================" -ForegroundColor Cyan
Write-Host "MSIX Package Verification" -ForegroundColor Cyan
Write-Host "==================================" -ForegroundColor Cyan
Write-Host "Package: $MsixPath" -ForegroundColor White
Write-Host ""

# Extract package
Write-Host "Extracting package..." -ForegroundColor Yellow
if (Test-Path $ExtractDir) {
    Remove-Item $ExtractDir -Recurse -Force
}
New-Item -ItemType Directory -Path $ExtractDir -Force | Out-Null

Add-Type -AssemblyName System.IO.Compression.FileSystem
[System.IO.Compression.ZipFile]::ExtractToDirectory($MsixPath, $ExtractDir)
Write-Host "  OK Extracted to: $ExtractDir" -ForegroundColor Green

if ($ExtractOnly) {
    Write-Host ""
    Write-Host "Extract-only mode: Skipping verification checks" -ForegroundColor Yellow
    exit 0
}

# Define required files
$RequiredFiles = @(
    @{ Path = "ef-overlay-tray.exe"; Description = "Tray helper executable (GUI)" }
    @{ Path = "ef-overlay.dll"; Description = "Overlay DLL" }
    @{ Path = "ef-overlay-injector.exe"; Description = "Injector executable" }
    @{ Path = "Assets\app.ico"; Description = "Tray icon (.ico file)" }
    @{ Path = "Assets\Square44x44Logo.png"; Description = "Store asset: 44x44 logo" }
    @{ Path = "Assets\Square150x150Logo.png"; Description = "Store asset: 150x150 logo" }
    @{ Path = "Assets\Wide310x150Logo.png"; Description = "Store asset: 310x150 logo" }
    @{ Path = "Assets\StoreLogo.png"; Description = "Store asset: Store logo" }
    @{ Path = "Assets\SplashScreen.png"; Description = "Store asset: Splash screen" }
    @{ Path = "AppxManifest.xml"; Description = "Package manifest" }
)

# Verify files
Write-Host ""
Write-Host "Verifying required files..." -ForegroundColor Yellow
$AllPresent = $true
$MissingFiles = @()

foreach ($file in $RequiredFiles) {
    $FullPath = Join-Path $ExtractDir $file.Path
    $Exists = Test-Path $FullPath
    
    if ($Exists) {
        $FileSize = (Get-Item $FullPath).Length
        $SizeKB = [math]::Round($FileSize / 1KB, 1)
        Write-Host "  OK $($file.Path) ($SizeKB KB)" -ForegroundColor Green
    } else {
        Write-Host "  MISSING: $($file.Path) - $($file.Description)" -ForegroundColor Red
        $AllPresent = $false
        $MissingFiles += $file
    }
}

# Check for PowerShell script accidentally included
Write-Host ""
Write-Host "Checking for accidentally included scripts..." -ForegroundColor Yellow
$Scripts = Get-ChildItem $ExtractDir -Recurse -Filter "*.ps1"
if ($Scripts.Count -gt 0) {
    Write-Host "  WARNING: PowerShell scripts found in package (should not be present):" -ForegroundColor Yellow
    foreach ($script in $Scripts) {
        Write-Host "    - $($script.Name)" -ForegroundColor Yellow
    }
} else {
    Write-Host "  OK No PowerShell scripts found" -ForegroundColor Green
}

# Verify manifest
Write-Host ""
Write-Host "Checking manifest..." -ForegroundColor Yellow
$ManifestPath = Join-Path $ExtractDir "AppxManifest.xml"
[xml]$Manifest = Get-Content $ManifestPath
$Version = $Manifest.Package.Identity.Version
$Publisher = $Manifest.Package.Identity.Publisher
$DisplayName = $Manifest.Package.Properties.DisplayName

Write-Host "  Version: $Version" -ForegroundColor Cyan
Write-Host "  Publisher: $Publisher" -ForegroundColor Cyan
Write-Host "  Display Name: $DisplayName" -ForegroundColor Cyan

# Final verdict
Write-Host ""
Write-Host "==================================" -ForegroundColor Cyan
if ($AllPresent) {
    Write-Host "VERIFICATION PASSED" -ForegroundColor Green
    Write-Host "==================================" -ForegroundColor Green
    Write-Host ""
    Write-Host "This package contains all required files." -ForegroundColor Green
    Write-Host "Safe to upload to Microsoft Store." -ForegroundColor Green
    Write-Host ""
    Write-Host "Extracted contents at: $ExtractDir" -ForegroundColor Cyan
    Write-Host "Review contents manually if needed before upload." -ForegroundColor Yellow
    exit 0
} else {
    Write-Host "VERIFICATION FAILED" -ForegroundColor Red
    Write-Host "==================================" -ForegroundColor Red
    Write-Host ""
    Write-Host "Missing files ($($MissingFiles.Count)):" -ForegroundColor Red
    foreach ($file in $MissingFiles) {
        Write-Host "  - $($file.Path): $($file.Description)" -ForegroundColor Red
    }
    Write-Host ""
    Write-Host "DO NOT UPLOAD THIS PACKAGE" -ForegroundColor Red
    Write-Host "Fix the build script and rebuild before uploading." -ForegroundColor Yellow
    exit 1
}
