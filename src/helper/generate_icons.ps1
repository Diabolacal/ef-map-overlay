# Generate MSIX Icons from EF Map Logo
# 
# Purpose: Resize ef-map-logo.png to required MSIX icon sizes
# 
# Requirements: Windows (uses built-in .NET System.Drawing)
# 
# Output: Creates 44x44, 50x50, 150x150, and 310x150 PNG icons in Assets folder

param(
    [string]$SourceLogo = "C:\EF-Map-main\eve-frontier-map\public\ef-map-logo.png",
    [string]$OutputDir = (Join-Path $PSScriptRoot "Assets")
)

$ErrorActionPreference = 'Stop'

# Validate source exists
if (!(Test-Path $SourceLogo)) {
    Write-Error "Source logo not found: $SourceLogo"
}

# Create output directory
if (!(Test-Path $OutputDir)) {
    New-Item $OutputDir -ItemType Directory | Out-Null
}

# Load .NET imaging assemblies
Add-Type -AssemblyName System.Drawing

# Load source image
$sourceImage = [System.Drawing.Image]::FromFile($SourceLogo)
Write-Host "Loaded source image: $($sourceImage.Width)x$($sourceImage.Height)"

# Icon sizes required by MSIX
$iconSizes = @(
    @{ Name = "Square44x44Logo.png"; Width = 44; Height = 44 },
    @{ Name = "StoreLogo.png"; Width = 50; Height = 50 },
    @{ Name = "Square150x150Logo.png"; Width = 150; Height = 150 },
    @{ Name = "Wide310x150Logo.png"; Width = 310; Height = 150 }
)

foreach ($icon in $iconSizes) {
    $outputPath = Join-Path $OutputDir $icon.Name
    
    # Create bitmap with target size
    $bitmap = New-Object System.Drawing.Bitmap $icon.Width, $icon.Height
    
    # Create graphics context for high-quality resize
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    $graphics.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
    $graphics.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::HighQuality
    $graphics.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
    $graphics.CompositingQuality = [System.Drawing.Drawing2D.CompositingQuality]::HighQuality
    
    # Draw resized image
    $graphics.DrawImage($sourceImage, 0, 0, $icon.Width, $icon.Height)
    
    # Save as PNG
    $bitmap.Save($outputPath, [System.Drawing.Imaging.ImageFormat]::Png)
    
    $graphics.Dispose()
    $bitmap.Dispose()
    
    Write-Host "✅ Created $($icon.Name) ($($icon.Width)x$($icon.Height))"
}

$sourceImage.Dispose()

Write-Host ""
Write-Host "Icon generation complete! Icons saved to: $OutputDir"
Write-Host ""
Write-Host "Next steps:"
Write-Host "1. Review icons in $OutputDir"
Write-Host "2. Rebuild MSIX: .\build_msix.ps1"
Write-Host "3. Sign MSIX: .\sign_msix.ps1"
