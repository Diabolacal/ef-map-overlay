# Release Build Guide for LLMs

**Purpose**: This document provides exact steps for building and packaging Microsoft Store releases. Follow this process for ALL Store submissions.

## Critical Information

### Single Build Script
There is **ONLY ONE** packaging script: `build_msix.ps1`
- It uses the `$BuildConfig` parameter to select Debug or Release binaries
- The icon fix and all packaging logic is in this ONE script
- There is NO separate script for Release vs Debug builds

### Why This Matters
Previous failures occurred because:
1. v1.0.0: Wrong file type was packaged (PowerShell script instead of executable)
2. v1.0.1: Tray icon (`app.ico`) was missing from the package despite assurances
3. v1.0.2 (first attempt): Publisher name was overwritten with placeholder value, causing Store validation errors

**Never trust verbal confirmation** - always run the verification script.

## Complete Build Process

### Step 1: Build Release Binaries

```powershell
cd C:\ef-map-overlay\build
cmake --build . --config Release
```

**What this does**: Compiles optimized Release binaries (smaller, faster than Debug)

**Expected output**: Should see successful compilation messages for:
- `ef-overlay-helper.exe`
- `ef-overlay-tray.exe` (GUI helper)
- `ef-overlay.dll`
- `ef-overlay-injector.exe`

### Step 2: Package the MSIX

```powershell
cd C:\ef-map-overlay\packaging\msix
.\build_msix.ps1 -Version "X.Y.Z" -BuildConfig "Release"
```

**Replace `X.Y.Z`** with the actual version number (e.g., "1.0.2")

**What to look for in output**:
```
Copying binaries...
  OK ef-overlay-tray.exe
  OK ef-overlay.dll
  OK ef-overlay-injector.exe
Creating manifest...
Copying tray icon...
  OK app.ico copied for tray icon    ← MUST see this line
Creating package assets from EF-Map logo...
  OK Assets created from EF-Map logo
```

**If you DON'T see "OK app.ico copied for tray icon"**: STOP, the build is broken.

### Step 3: MANDATORY Verification

```powershell
.\verify_msix_contents.ps1
```

**Expected output**:
```
Verifying required files...
  OK ef-overlay-tray.exe (2393 KB)      ← Release size ~2.4 MB
  OK ef-overlay.dll (809 KB)            ← Release size ~809 KB
  OK ef-overlay-injector.exe (36.5 KB)  ← Release size ~37 KB
  OK Assets\app.ico (0.4 KB)            ← MUST be present
  [... other files ...]

Checking manifest...
  Version: 1.0.2.0
  Publisher: CN=9523ACA0-C1D5-4790-88D6-D95FA23F0EF9  ← CRITICAL: Must match Partner Center
  Display Name: EF-Map Overlay Helper

VERIFICATION PASSED
```

**CRITICAL**: The Publisher MUST be `CN=9523ACA0-C1D5-4790-88D6-D95FA23F0EF9`. If it shows `CN=YOUR_NAME_HERE` or any other value, the package will be REJECTED by the Store.

**If verification FAILS**: DO NOT upload. Fix the issue, rebuild, and re-verify.

### Step 4: File Location

The MSIX package will be at:
```
C:\ef-map-overlay\releases\EFMapHelper-vX.Y.Z.msix
```

This is the file to upload to Microsoft Partner Center.

## Expected File Sizes (Release Build)

| File | Release Size | Debug Size |
|------|--------------|------------|
| ef-overlay-tray.exe | ~2.4 MB | ~6.9 MB |
| ef-overlay.dll | ~809 KB | ~4.8 MB |
| ef-overlay-injector.exe | ~37 KB | ~164 KB |
| app.ico | 0.4 KB | 0.4 KB |

**Red flag**: If tray exe is only a few KB, it's a script (wrong file).

## Common Mistakes to Avoid

### ❌ DON'T: Build Debug for Store
```powershell
# WRONG - This creates Debug binaries (too large)
cmake --build . --config Debug
```

### ❌ DON'T: Skip verification
```powershell
# WRONG - Never upload without verification
.\build_msix.ps1 -Version "1.0.2" -BuildConfig "Release"
# ... then immediately upload without running verify_msix_contents.ps1
```

### ❌ DON'T: Trust verbal confirmation
Never say "I've verified the files are correct" without actually running `verify_msix_contents.ps1`.

### ✅ DO: Follow the exact sequence
```powershell
# 1. Build Release
cd C:\ef-map-overlay\build
cmake --build . --config Release

# 2. Package
cd C:\ef-map-overlay\packaging\msix
.\build_msix.ps1 -Version "1.0.2" -BuildConfig "Release"

# 3. Verify (MANDATORY)
.\verify_msix_contents.ps1

# 4. Only upload if verification passes
```

## Troubleshooting

### Issue: "app.ico copied for tray icon" doesn't appear in build output

**Cause**: The packaging script is missing the icon copy step.

**Fix**: Check that `build_msix.ps1` contains this code block (should be around line 64):
```powershell
# Copy tray icon (.ico) that the helper tries to load at runtime
Write-Host "Copying tray icon..."
$HelperIconPath = Join-Path $RepoRoot "src\helper\Assets\app.ico"
if (Test-Path $HelperIconPath) {
    Copy-Item $HelperIconPath -Destination "$StagingDir\Assets\app.ico"
    Write-Host "  OK app.ico copied for tray icon" -ForegroundColor Green
} else {
    Write-Host "  WARNING: Tray icon not found at $HelperIconPath" -ForegroundColor Yellow
}
```

### Issue: Verification script reports "app.ico MISSING"

**Cause**: The icon file doesn't exist at `src\helper\Assets\app.ico` or the packaging script didn't copy it.

**Fix**: 
1. Verify icon exists: `Test-Path C:\ef-map-overlay\src\helper\Assets\app.ico`
2. If missing, regenerate it (see `src\helper\generate_ico.ps1`)
3. Rebuild the package

### Issue: File sizes don't match Release expectations

**Cause**: Built with Debug configuration instead of Release.

**Fix**: Delete the package, rebuild with `--config Release`, repackage, re-verify.

### Issue: Store validation errors - "Invalid package publisher name" or "Invalid package family name"

**Cause**: The `build_msix.ps1` script is incorrectly modifying the Publisher field in the manifest.

**Symptoms**:
- Verification script shows: `Publisher: CN=YOUR_NAME_HERE` (WRONG)
- Store upload fails with validation errors about publisher/family name mismatch

**Fix**: 
1. Check that `build_msix.ps1` does NOT have a line like:
   ```powershell
   $ManifestContent = $ManifestContent -replace 'Publisher="[^"]*"', "Publisher=`"$PublisherName`""
   ```
2. The script should have a comment instead:
   ```powershell
   # DO NOT modify Publisher - it's already set correctly in AppxManifest.xml from Partner Center
   ```
3. The base `AppxManifest.xml` file already has the correct Publisher from Partner Center
4. Rebuild and re-verify - Publisher should show: `CN=9523ACA0-C1D5-4790-88D6-D95FA23F0EF9`

## After Upload to Store

1. Wait for Microsoft certification (~24 hours)
2. Once approved, test the Store-signed package on a clean Windows machine
3. Confirm:
   - ✅ Helper installs successfully
   - ✅ Tray icon shows EF-Map logo (not generic Windows icon)
   - ✅ Injection works
   - ✅ Overlay renders

## Version History

| Version | Date | Issue Fixed |
|---------|------|-------------|
| 1.0.0 | 2025-10-30 | Initial submission (rejected - wrong file type) |
| 1.0.0 | 2025-10-30 | Resubmission (approved - correct executable) |
| 1.0.1 | 2025-10-31 | Bug fix (approved - but missing tray icon) |
| 1.0.2 (attempt 1) | 2025-10-31 | Icon fix (rejected - Publisher overwritten with placeholder) |
| 1.0.2 (attempt 2) | 2025-10-31 | Fixed Publisher + icon (ready for upload) |

---

**Last updated**: 2025-10-31 (After v1.0.2 Publisher fix - removed script line that overwrote manifest Publisher)
