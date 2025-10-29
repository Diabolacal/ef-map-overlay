# Placeholder Asset Files

For MSIX packaging, replace these placeholders with actual .png files:

- **Square44x44Logo.png** - 44×44px app icon (taskbar, tile)
- **Square150x150Logo.png** - 150×150px medium tile
- **Wide310x150Logo.png** - 310×150px wide tile
- **StoreLogo.png** - 50×50px Store listing icon

**For testing (unsigned MSIX):**
You can use any valid PNG files with matching dimensions. The package will build as long as the files exist.

**For production (signed MSIX + Store):**
Use high-quality icons with transparency. Microsoft Store requires specific guidelines:
- https://learn.microsoft.com/en-us/windows/apps/design/style/iconography/app-icon-design

**Quick Placeholder Generation:**
Use any image editor to create solid-color squares at the required dimensions, or generate via PowerShell:

```powershell
# Creates 1×1 pixel PNGs (Windows will scale them, looks ugly but validates structure)
Add-Type -AssemblyName System.Drawing
$sizes = @(44, 150, 310, 50)
foreach ($size in $sizes) {
    $bmp = New-Object System.Drawing.Bitmap(1, 1)
    $bmp.SetPixel(0, 0, [System.Drawing.Color]::Orange)
    $bmp.Save("Square${size}x${size}Logo.png", [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
}
```

For now, I'll create minimal 1px placeholders so the package builds.
