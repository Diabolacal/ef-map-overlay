# Simple release packager - avoids HERE-string parser issues
param(
    [string]$Version = "1.0.0",
    [string]$BuildConfig = "Debug"
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

# Clean existing
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
    Write-Host "  OK $(Split-Path $binary -Leaf)" -ForegroundColor Green
}

# Copy pre-made README and launcher from tools directory
if (Test-Path "C:\ef-map-overlay\tools\README_TEMPLATE.txt") {
    $readme = Get-Content "C:\ef-map-overlay\tools\README_TEMPLATE.txt" -Raw
    $readme = $readme -replace '\{VERSION\}', $Version
    $readme | Out-File -FilePath "$packageDir\README.txt" -Encoding UTF8
    Write-Host "  OK README.txt" -ForegroundColor Green
}

if (Test-Path "C:\ef-map-overlay\tools\launch_helper_template.ps1") {
    Copy-Item "C:\ef-map-overlay\tools\launch_helper_template.ps1" -Destination "$packageDir\launch_helper.ps1"
    Write-Host "  OK launch_helper.ps1" -ForegroundColor Green
}

Write-Host "`nCreating ZIP..." -ForegroundColor Yellow

if (Test-Path $zipPath) {
    Remove-Item -Path $zipPath -Force
}

Compress-Archive -Path "$packageDir\*" -DestinationPath $zipPath -CompressionLevel Optimal
Remove-Item -Path $packageDir -Recurse -Force

Write-Host "`n==================================" -ForegroundColor Cyan
Write-Host "Release created: $zipPath" -ForegroundColor Green
Write-Host "Size: $([math]::Round((Get-Item $zipPath).Length / 1MB, 2)) MB" -ForegroundColor Yellow
Write-Host "==================================" -ForegroundColor Cyan
