# Generate Microsoft Store Display Logos
# This script creates the required Store listing logo sizes from existing package logos

param(
    [string]$SourceLogo = ".\staging\Assets\Square150x150Logo.png",
    [string]$OutputDir = ".\store_logos"
)

Write-Host "=== Microsoft Store Logo Generator ===" -ForegroundColor Cyan
Write-Host ""

# Ensure output directory exists
if (!(Test-Path $OutputDir)) {
    New-Item -ItemType Directory -Path $OutputDir | Out-Null
    Write-Host "Created output directory: $OutputDir" -ForegroundColor Green
}

# Load .NET assemblies for image manipulation
Add-Type -AssemblyName System.Drawing

try {
    # Load source image
    if (!(Test-Path $SourceLogo)) {
        Write-Host "ERROR: Source logo not found: $SourceLogo" -ForegroundColor Red
        exit 1
    }
    
    Write-Host "Loading source logo: $SourceLogo" -ForegroundColor Yellow
    $sourceImage = [System.Drawing.Image]::FromFile((Resolve-Path $SourceLogo))
    Write-Host "Source dimensions: $($sourceImage.Width)x$($sourceImage.Height)" -ForegroundColor Gray
    Write-Host ""
    
    # Define required Store logo sizes (1:1 ratio only)
    $sizes = @(
        @{Width=300; Height=300; Name="AppIcon_300x300.png"},
        @{Width=150; Height=150; Name="AppIcon_150x150.png"},
        @{Width=71; Height=71; Name="AppIcon_71x71.png"}
    )
    
    foreach ($size in $sizes) {
        $outputPath = Join-Path (Resolve-Path $OutputDir) $size.Name
        Write-Host "Creating $($size.Name) ($($size.Width)x$($size.Height))..." -ForegroundColor Cyan
        
        # Create new bitmap with target size
        $bitmap = New-Object System.Drawing.Bitmap $size.Width, $size.Height
        $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
        
        # Set high quality rendering
        $graphics.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
        $graphics.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::HighQuality
        $graphics.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
        $graphics.CompositingQuality = [System.Drawing.Drawing2D.CompositingQuality]::HighQuality
        
        # Draw resized image
        $graphics.DrawImage($sourceImage, 0, 0, $size.Width, $size.Height)
        
        # Save to file
        $bitmap.Save($outputPath, [System.Drawing.Imaging.ImageFormat]::Png)
        
        # Cleanup
        $graphics.Dispose()
        $bitmap.Dispose()
        
        # Verify created file
        $fileInfo = Get-Item $outputPath
        $sizeKB = [math]::Round($fileInfo.Length / 1KB, 2)
        Write-Host "  [OK] Created: " -NoNewline -ForegroundColor Green
        Write-Host "$($fileInfo.Name) " -NoNewline
        Write-Host "($sizeKB KB)" -ForegroundColor Gray
    }
    
    Write-Host ""
    Write-Host "=== Logo Generation Complete ===" -ForegroundColor Green
    Write-Host ""
    Write-Host "Store logos saved to: $OutputDir" -ForegroundColor Cyan
    Write-Host ""
    Write-Host "Next steps:" -ForegroundColor Yellow
    Write-Host "  1. Review generated logos in $OutputDir" -ForegroundColor Gray
    Write-Host "  2. Upload to Microsoft Store submission:" -ForegroundColor Gray
    Write-Host "     - 1:1 App icon (300x300): AppIcon_300x300.png" -ForegroundColor Gray
    Write-Host "     - 1:1 (150x150): AppIcon_150x150.png" -ForegroundColor Gray
    Write-Host "     - 1:1 (71x71): AppIcon_71x71.png" -ForegroundColor Gray
    Write-Host ""
    
} catch {
    Write-Host "ERROR: $_" -ForegroundColor Red
    exit 1
} finally {
    # Cleanup
    if ($sourceImage) {
        $sourceImage.Dispose()
    }
}

Write-Host "Done!" -ForegroundColor Green
