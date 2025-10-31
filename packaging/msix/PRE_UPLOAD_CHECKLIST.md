# Store Submission Pre-Upload Checklist

**MANDATORY**: Complete this checklist before every Microsoft Store submission.

## Critical Context
We have had TWO failed Store submissions due to incomplete packages:
1. **v1.0.0**: PowerShell script was packaged instead of the helper executable
2. **v1.0.1**: Tray icon (.ico file) was missing from the package

These failures cost **24+ hours** per submission in Store review time.

## Pre-Upload Verification (MUST DO)

### 1. Run Verification Script
```powershell
cd C:\ef-map-overlay\packaging\msix
.\verify_msix_contents.ps1
```

**STOP**: If verification fails, DO NOT upload. Fix the issue and rebuild.

### 2. Visual Inspection Required Files

After the verification script passes, manually confirm the extracted package contains:

**Release Build (Store Submission):**
- [ ] `ef-overlay-tray.exe` (GUI helper, ~2.4 MB Release / ~6.9 MB Debug)
- [ ] `ef-overlay.dll` (Overlay module, ~809 KB Release / ~4.8 MB Debug)
- [ ] `ef-overlay-injector.exe` (Injector, ~36.5 KB Release / ~164 KB Debug)
- [ ] `Assets\app.ico` (Tray icon, 0.4 KB) **← This was missing in v1.0.1**
- [ ] `Assets\Square44x44Logo.png`
- [ ] `Assets\Square150x150Logo.png`
- [ ] `Assets\Wide310x150Logo.png`
- [ ] `Assets\StoreLogo.png`
- [ ] `Assets\SplashScreen.png`
- [ ] `AppxManifest.xml`

### 3. File Type Check

Confirm these are ACTUAL compiled binaries, not scripts:

```powershell
# Check file signatures
Get-Item ".\extracted_<VERSION>\ef-overlay-tray.exe" | Select-Object Name, Length
Get-Item ".\extracted_<VERSION>\ef-overlay.dll" | Select-Object Name, Length
Get-Item ".\extracted_<VERSION>\Assets\app.ico" | Select-Object Name, Length
```

Expected output (Release build):
- `ef-overlay-tray.exe`: ~2400 KB (Release) or ~6900 KB (Debug) - NOT a small PowerShell script
- `ef-overlay.dll`: ~809 KB (Release) or ~4800 KB (Debug)
- `app.ico`: < 1 KB (but MUST exist)

### 4. Double-Check Icon Exists

**CRITICAL**: The tray icon was missing in v1.0.1 despite previous assurances.

```powershell
Test-Path ".\extracted_<VERSION>\Assets\app.ico"
```

**Expected**: `True`

If `False`: STOP, rebuild, and re-verify.

### 5. Version Number Check

Confirm the version in `AppxManifest.xml` matches your intended release:

```powershell
[xml]$manifest = Get-Content ".\extracted_<VERSION>\AppxManifest.xml"
$manifest.Package.Identity.Version
```

### 6. No Accidental Script Inclusion

Confirm no PowerShell scripts were accidentally bundled:

```powershell
Get-ChildItem ".\extracted_<VERSION>" -Recurse -Filter "*.ps1"
```

**Expected**: No results (empty list)

## Build Process Reminder

### Correct Build Sequence
```powershell
# 1. Build the project (Release config for Store)
cd C:\ef-map-overlay\build
cmake --build . --config Release

# 2. Package the MSIX
cd C:\ef-map-overlay\packaging\msix
.\build_msix.ps1 -Version "X.Y.Z" -BuildConfig "Release"

# 3. VERIFY (MANDATORY)
.\verify_msix_contents.ps1

# 4. Only if verification passes: Upload to Partner Center
```

## Upload to Partner Center

1. Log in to https://partner.microsoft.com/dashboard
2. Navigate to Apps and games → EF-Map Overlay Helper
3. Create new submission
4. Upload the **verified** MSIX file
5. Update release notes if needed
6. Submit for certification

## After Approval

Test the Store-signed package on a clean Windows machine:
1. Install from Microsoft Store
2. Launch helper
3. **Confirm tray icon shows "EF-Map" logo** (not generic Windows folder icon)
4. Confirm injection works
5. Confirm sample overlay state works

## Failure Recovery

If a package is rejected or found incomplete after upload:
1. Document the issue in `docs/decision-log.md`
2. Update this checklist if a new check is needed
3. Update `verify_msix_contents.ps1` to catch the issue
4. Rebuild and re-verify before resubmitting

---

Last updated: 2025-10-31 (After v1.0.1 icon omission incident)
