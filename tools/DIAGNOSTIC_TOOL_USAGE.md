# EF-Map Overlay - Injection Failure Diagnostic Tool

## What This Tool Does

This PowerShell script collects detailed information about your system to help diagnose why the overlay won't inject into EVE Frontier, even though the helper shows as "connected" in your browser.

## Requirements

- Windows 10 or Windows 11
- PowerShell 5.1+ (included with Windows)
- Both **helper** and **game** must be running when you run this script

## How to Use

### Step 1: Download the Script

Save `diagnose_injection_failure.ps1` to your Desktop or Downloads folder.

### Step 2: Prepare Your System

1. **Start EVE Frontier** - Make sure the game is fully loaded
2. **Start EF-Map Overlay Helper** - Ensure it shows "connected" in your browser
3. **Try clicking "Start Overlay"** in the helper UI - Verify the injection still fails

### Step 3: Run the Script

**Option A: Right-click method (easiest)**
1. Right-click on `diagnose_injection_failure.ps1`
2. Select "Run with PowerShell"
3. Wait for it to complete (10-30 seconds)
4. Script will create a report file on your Desktop

**Option B: PowerShell window method**
1. Press `Win + X` → Select "Windows PowerShell"
2. Navigate to where you saved the script:
   ```powershell
   cd $env:USERPROFILE\Desktop
   ```
3. Run the script:
   ```powershell
   .\diagnose_injection_failure.ps1
   ```

### Step 4: Send the Report

1. The script saves a report file to your Desktop named:
   ```
   ef-overlay-diagnostics-YYYYMMDD-HHmmss.txt
   ```

2. Open the file and review the summary (beginning of file)

3. **Send the entire file** to the developer via Discord

4. Also mention:
   - What happens when you click "Start Overlay" (any error messages?)
   - Do you see a UAC prompt asking for administrator permission?
   - Does the helper tray icon show any errors?

## Advanced Options

### Include Full Helper Logs

If the developer asks for full logs, run with `-IncludeLogs`:

```powershell
.\diagnose_injection_failure.ps1 -IncludeLogs
```

This increases report size but provides more detailed information.

### Custom Output Location

Save report to a specific location:

```powershell
.\diagnose_injection_failure.ps1 -OutputFile "C:\MyFolder\diagnostics.txt"
```

## What the Script Collects

The diagnostic tool collects:

✅ **System information** - Windows version, architecture
✅ **Process details** - Helper and game process status, elevation levels, session IDs
✅ **UAC configuration** - User Account Control settings
✅ **Installation type** - Microsoft Store vs development build
✅ **DLL injection status** - Whether overlay DLL loaded into game (if detectable)
✅ **Helper logs** - Recent errors and warnings from helper log files
✅ **Security software** - Antivirus products that might interfere
✅ **Helper API status** - Whether helper HTTP endpoints are responding

❌ **Does NOT collect** - Personal files, passwords, game account info, or chat logs

## Common Issues Detected

The script automatically detects and reports:

### 🔴 CRITICAL: Elevation Mismatch
**Most common problem** - Helper and game running with different administrator privileges.

**Fix:**
1. Close both helper and game completely
2. Launch helper **WITHOUT** "Run as administrator"
3. Launch game **WITHOUT** "Run as administrator"
4. Click "Start Overlay" (accept UAC prompt if it appears)

### 🔴 CRITICAL: Session ID Mismatch
Helper and game running in different Windows sessions (Remote Desktop or multiple users logged in).

**Fix:**
1. Log out all other Windows users
2. Close any Remote Desktop sessions
3. Restart helper and game on the same user account

### 🔴 CRITICAL: DLL Not Loaded
Overlay DLL failed to inject into game process.

**Fix:**
1. Check helper logs for specific injection errors
2. Temporarily disable antivirus
3. Verify game is running in DirectX 12 mode
4. Try running helper as Administrator

### ⚠️ WARNING: Microsoft Store Version
MSIX AppContainer restrictions may prevent injection.

**Fix:** Contact developer for alternative installation package (development build)

### ⚠️ WARNING: Security Software Detected
Antivirus may block DLL injection.

**Fix:** Add exceptions for:
- `ef-overlay-helper.exe`
- `ef-overlay.dll`
- `ef-overlay-injector.exe`
- `exefile.exe` (game process - allow DLL injection)

## Troubleshooting the Script Itself

### "Cannot run scripts" error

If you see an error about execution policy:

1. Open PowerShell **as Administrator**
2. Run:
   ```powershell
   Set-ExecutionPolicy -ExecutionPolicy RemoteSigned -Scope CurrentUser
   ```
3. Try running the diagnostic script again

### Script hangs or freezes

- Make sure helper and game are both running before starting script
- Close any process monitoring tools (they can interfere)
- Try closing and restarting PowerShell

### No report file created

- Check if you have write permissions to Desktop
- Try running script as Administrator
- Use custom output path: `.\diagnose_injection_failure.ps1 -OutputFile "C:\Temp\report.txt"`

## Privacy & Security

The diagnostic script:
- Runs entirely on your local machine
- Does not send any data to external servers
- Only writes a text file to your Desktop
- Does not modify any system settings
- Does not require administrator privileges to run (but provides more info if elevated)

You control when and if you send the report file to anyone.

## Questions?

If you have questions about the diagnostic tool or your results, ask in the Discord support channel!

## Version History

- **v1.0.0** (2025-11-06) - Initial release
  - System information collection
  - Process elevation/session detection
  - DLL injection status checking
  - Helper log parsing
  - Security software detection
  - Automated issue recommendations
