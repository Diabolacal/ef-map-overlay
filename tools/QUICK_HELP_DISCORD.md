# 🛠️ EF-Map Overlay Injection Failure - Quick Help

## 📥 Download & Run Diagnostic Tool

**Having trouble with overlay not injecting?** Run this diagnostic script and send me the results!

### Quick Steps:

1. **Download the script:** `diagnose_injection_failure.ps1`
2. **Make sure BOTH are running:**
   - ✅ EVE Frontier game
   - ✅ EF-Map Overlay Helper (showing "connected" in browser)
3. **Right-click the script** → "Run with PowerShell"
4. **Send me the report file** from your Desktop

### What it checks:
- Process elevation levels (most common issue!)
- Windows session conflicts
- DLL injection status
- Helper logs & errors
- Security software interference
- Installation type (Store vs Dev build)

---

## 🔥 Most Common Fixes (Before Running Diagnostic)

### 1️⃣ Elevation Mismatch (90% of cases)
**Problem:** Helper and game have different admin permissions

**Fix:**
```
1. Close helper completely
2. Close game completely
3. Launch helper WITHOUT "Run as administrator"
4. Launch game WITHOUT "Run as administrator"
5. Click "Start Overlay" in helper
6. Accept UAC prompt if it appears
```

### 2️⃣ Multiple Windows Users Logged In
**Problem:** Shared memory can't cross user sessions

**Fix:**
- Log out all other Windows users
- Close Remote Desktop sessions
- Restart both helper and game

### 3️⃣ Antivirus Blocking Injection
**Problem:** Security software sees DLL injection as suspicious

**Fix:** Add exceptions for:
- `ef-overlay-helper.exe`
- `ef-overlay.dll`
- `ef-overlay-injector.exe`
- `exefile.exe` (game - allow DLL injection)

---

## 📋 What to Include When Asking for Help

Send me:
1. ✅ The diagnostic report file (from Desktop)
2. ✅ What happens when you click "Start Overlay"? (Error message? Nothing?)
3. ✅ Do you see a UAC prompt? (Yes/No)
4. ✅ Any messages from helper tray icon?
5. ✅ Windows version (10 or 11?)
6. ✅ Microsoft Store version or dev build?

---

## 🚫 Common Script Issues

### "Cannot run scripts" error
**Fix:**
```powershell
# Open PowerShell as Administrator, then run:
Set-ExecutionPolicy -ExecutionPolicy RemoteSigned -Scope CurrentUser
```

### Script doesn't create report file
**Fix:**
```powershell
# Run with custom location:
.\diagnose_injection_failure.ps1 -OutputFile "C:\Temp\report.txt"
```

---

## 🔒 Privacy & Security

The diagnostic script:
- ✅ Runs only on your local machine
- ✅ Creates a text file report (no auto-upload)
- ✅ Does NOT modify system settings
- ✅ Does NOT collect passwords or personal files
- ✅ You control when/if you send the report

---

## 📚 Full Documentation

For detailed usage instructions, see: **DIAGNOSTIC_TOOL_USAGE.md**

---

**Need Help?** Ask in Discord support channel!
