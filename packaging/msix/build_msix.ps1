# MSIX Package Builder for EF Map Overlay Helper
# This creates an MSIX package ready for Microsoft Store submission

param(
    [string]$Version = "1.0.0",
    [string]$BuildConfig = "Release"
)

$ErrorActionPreference = "Stop"

# Paths
$RepoRoot = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$BuildDir = Join-Path $RepoRoot "build\src"
$PackagingDir = Join-Path $RepoRoot "packaging\msix"
$OutputDir = Join-Path $RepoRoot "releases"
$StagingDir = Join-Path $PackagingDir "staging"

# SDK Tools (adjust path if different Windows SDK version)
$MakeAppx = "C:\Program Files (x86)\Windows Kits\10\bin\10.0.19041.0\x64\makeappx.exe"
$SignTool = "C:\Program Files (x86)\Windows Kits\10\bin\10.0.19041.0\x64\signtool.exe"

Write-Host "Creating EF Map Helper v$Version MSIX package..." -ForegroundColor Cyan
Write-Host ""

# Clean and create staging directory
if (Test-Path $StagingDir) {
    Remove-Item $StagingDir -Recurse -Force
}
New-Item -ItemType Directory -Path $StagingDir -Force | Out-Null
New-Item -ItemType Directory -Path "$StagingDir\Assets" -Force | Out-Null

# Copy binaries
Write-Host "Copying binaries..."
$BinariesToCopy = @(
    @{ Source = "$BuildDir\helper\$BuildConfig\ef-overlay-tray.exe"; Dest = "ef-overlay-tray.exe" }
    @{ Source = "$BuildDir\overlay\$BuildConfig\ef-overlay.dll"; Dest = "ef-overlay.dll" }
    @{ Source = "$BuildDir\injector\$BuildConfig\ef-overlay-injector.exe"; Dest = "ef-overlay-injector.exe" }
)

foreach ($item in $BinariesToCopy) {
    if (Test-Path $item.Source) {
        Copy-Item $item.Source -Destination (Join-Path $StagingDir $item.Dest)
        Write-Host "  OK $($item.Dest)" -ForegroundColor Green
    } else {
        Write-Host "  MISSING: $($item.Source)" -ForegroundColor Red
        exit 1
    }
}

# Copy and update manifest
Write-Host "Creating manifest..."
$ManifestContent = Get-Content "$PackagingDir\AppxManifest.xml" -Raw
# Replace Identity Version only (not XML declaration version!)
$ManifestContent = $ManifestContent -replace '<Identity([^>]*?)Version="[\d\.]+"', "<Identity`$1Version=`"$Version.0`""
# DO NOT modify Publisher - it's already set correctly in AppxManifest.xml from Partner Center
# PublisherDisplayName should always be "Ef-Map" (from Partner Center)
$ManifestContent = $ManifestContent -replace '<PublisherDisplayName>[^<]*</PublisherDisplayName>', "<PublisherDisplayName>Ef-Map</PublisherDisplayName>"
# Write with UTF-8 NO BOM to avoid XML parser issues
$Utf8NoBom = New-Object System.Text.UTF8Encoding $false
[System.IO.File]::WriteAllText("$StagingDir\AppxManifest.xml", $ManifestContent, $Utf8NoBom)

# Copy tray icon (.ico) that the helper tries to load at runtime
Write-Host "Copying tray icon..."
$HelperIconPath = Join-Path $RepoRoot "src\helper\Assets\app.ico"
if (Test-Path $HelperIconPath) {
    Copy-Item $HelperIconPath -Destination "$StagingDir\Assets\app.ico"
    Write-Host "  OK app.ico copied for tray icon" -ForegroundColor Green
} else {
    Write-Host "  WARNING: Tray icon not found at $HelperIconPath" -ForegroundColor Yellow
}

# Create package assets from EF-Map logo
Write-Host "Creating package assets from EF-Map logo..."

# Source logo from main EF-Map repository
$SourceLogoPath = "C:\EF-Map-main\eve-frontier-map\src\assets\logo\logo.png"

if (Test-Path $SourceLogoPath) {
    Add-Type -AssemblyName System.Drawing
    $sourceLogo = [System.Drawing.Image]::FromFile($SourceLogoPath)
    
    # Function to resize logo to required dimensions
    function New-ResizedLogo {
        param([string]$Path, [int]$Width, [int]$Height, $SourceImage)
        
        $bitmap = New-Object System.Drawing.Bitmap $Width, $Height
        $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
        $graphics.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
        $graphics.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::HighQuality
        $graphics.DrawImage($SourceImage, 0, 0, $Width, $Height)
        $graphics.Dispose()
        $bitmap.Save($Path, [System.Drawing.Imaging.ImageFormat]::Png)
        $bitmap.Dispose()
    }
    
    # Create required Store assets from EF-Map logo
    New-ResizedLogo -Path "$StagingDir\Assets\Square44x44Logo.png" -Width 44 -Height 44 -SourceImage $sourceLogo
    New-ResizedLogo -Path "$StagingDir\Assets\Square150x150Logo.png" -Width 150 -Height 150 -SourceImage $sourceLogo
    New-ResizedLogo -Path "$StagingDir\Assets\Wide310x150Logo.png" -Width 310 -Height 150 -SourceImage $sourceLogo
    New-ResizedLogo -Path "$StagingDir\Assets\StoreLogo.png" -Width 50 -Height 50 -SourceImage $sourceLogo
    New-ResizedLogo -Path "$StagingDir\Assets\SplashScreen.png" -Width 620 -Height 300 -SourceImage $sourceLogo
    
    $sourceLogo.Dispose()
    Write-Host "  OK Assets created from EF-Map logo" -ForegroundColor Green
} else {
    Write-Host "  WARNING: EF-Map logo not found at $SourceLogoPath - using placeholders" -ForegroundColor Yellow
    # Fallback to placeholder if logo not found
    function New-PlaceholderImage {
        param([string]$Path, [int]$Width, [int]$Height)
        Add-Type -AssemblyName System.Drawing
        $bmp = New-Object System.Drawing.Bitmap($Width, $Height)
        $graphics = [System.Drawing.Graphics]::FromImage($bmp)
        $graphics.Clear([System.Drawing.Color]::FromArgb(41, 128, 185))
        $graphics.Dispose()
        $bmp.Save($Path, [System.Drawing.Imaging.ImageFormat]::Png)
        $bmp.Dispose()
    }
    New-PlaceholderImage -Path "$StagingDir\Assets\Square44x44Logo.png" -Width 44 -Height 44
    New-PlaceholderImage -Path "$StagingDir\Assets\Square150x150Logo.png" -Width 150 -Height 150
    New-PlaceholderImage -Path "$StagingDir\Assets\Wide310x150Logo.png" -Width 310 -Height 150
    New-PlaceholderImage -Path "$StagingDir\Assets\StoreLogo.png" -Width 50 -Height 50
    New-PlaceholderImage -Path "$StagingDir\Assets\SplashScreen.png" -Width 620 -Height 300
    Write-Host "  OK Placeholder assets created" -ForegroundColor Green
}

# Create MSIX package
Write-Host ""
Write-Host "Creating MSIX package..."
$MsixPath = Join-Path $OutputDir "EFMapHelper-v$Version.msix"
& $MakeAppx pack /d $StagingDir /p $MsixPath /o

if ($LASTEXITCODE -ne 0) {
    Write-Host "ERROR: Failed to create MSIX package" -ForegroundColor Red
    exit 1
}

Write-Host ""
Write-Host "==================================" -ForegroundColor Green
Write-Host "MSIX created: $MsixPath" -ForegroundColor Green
$FileSize = (Get-Item $MsixPath).Length / 1MB
Write-Host "Size: $([math]::Round($FileSize, 2)) MB" -ForegroundColor Green
Write-Host "==================================" -ForegroundColor Green
Write-Host ""
Write-Host "NEXT STEPS:" -ForegroundColor Yellow
Write-Host "1. This MSIX is unsigned and FOR STORE SUBMISSION ONLY" -ForegroundColor Yellow
Write-Host "2. Do NOT try to install it locally (will fail)" -ForegroundColor Yellow
Write-Host "3. Upload to Microsoft Partner Center" -ForegroundColor Yellow
Write-Host "4. Microsoft will sign it during certification" -ForegroundColor Yellow
Write-Host ""
Write-Host "See packaging\msix\STORE_SUBMISSION_GUIDE.md for detailed steps" -ForegroundColor Cyan
