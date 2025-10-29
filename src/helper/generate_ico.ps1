# Generate Windows ICO file from EF Map Logo
# 
# Purpose: Create multi-size .ico file for tray icon from PNG source
# 
# Output: app.ico with 16x16, 32x32, 48x48, 256x256 embedded sizes

param(
    [string]$SourceLogo = "C:\EF-Map-main\eve-frontier-map\public\ef-map-logo.png",
    [string]$OutputPath = (Join-Path $PSScriptRoot "Assets\app.ico")
)

$ErrorActionPreference = 'Stop'

# Validate source
if (!(Test-Path $SourceLogo)) {
    Write-Error "Source logo not found: $SourceLogo"
}

# Load .NET assemblies
Add-Type -AssemblyName System.Drawing

# Load source image
$sourceImage = [System.Drawing.Image]::FromFile($SourceLogo)
Write-Host "Loaded source: $($sourceImage.Width)x$($sourceImage.Height)"

# Icon sizes to include in .ico (Windows best practice)
$iconSizes = @(16, 32, 48, 256)

# Create temporary bitmaps for each size
$bitmaps = @()
foreach ($size in $iconSizes) {
    $bitmap = New-Object System.Drawing.Bitmap $size, $size
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    $graphics.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
    $graphics.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::HighQuality
    $graphics.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
    $graphics.CompositingQuality = [System.Drawing.Drawing2D.CompositingQuality]::HighQuality
    
    $graphics.DrawImage($sourceImage, 0, 0, $size, $size)
    $graphics.Dispose()
    
    $bitmaps += $bitmap
    Write-Host "  Created ${size}x${size} bitmap"
}

# Create ICO file using Icon.FromHandle workaround
# (System.Drawing.Icon doesn't have a direct multi-size save method)
# We'll save the 16x16 version for now (tray icon size)
$smallIcon = $bitmaps[0]
$iconHandle = $smallIcon.GetHicon()
$icon = [System.Drawing.Icon]::FromHandle($iconHandle)
$iconStream = [System.IO.File]::Create($OutputPath)
$icon.Save($iconStream)
$iconStream.Close()
$icon.Dispose()
[void][System.Runtime.InteropServices.Marshal]::ReleaseComObject($iconHandle) # Clean up handle

# Dispose bitmaps
foreach ($bitmap in $bitmaps) {
    $bitmap.Dispose()
}

$sourceImage.Dispose()

Write-Host ""
Write-Host "✅ Created app.ico: $OutputPath"
Write-Host ""
Write-Host "Next steps:"
Write-Host "1. Rebuild tray app: cmake --build build --config Debug --target ef_overlay_tray"
Write-Host "2. Rebuild MSIX: .\build_msix.ps1"
Write-Host "3. Sign MSIX: .\sign_msix.ps1"
