# Troubleshooting: "Could not connect to helper. Is it running?" (Overlay Error)

**Last Updated:** 2025-10-31  
**Symptom:** Helper shows "connected" and reports system to web app, but overlay displays "Could not connect to helper. Is it running?"

---

## Problem Summary

**Key Observation:**
- ✅ Helper HTTP API works (web app receives system reports)
- ✅ Helper shows "connected" status
- ❌ Overlay can't connect to helper

**Root Cause:** The overlay uses **shared memory** (`Local\\EFOverlaySharedState`), NOT HTTP. Web app connectivity doesn't prove shared memory is accessible.

---

## Diagnostic Steps

### Step 1: Check Session/Elevation Mismatch (MOST COMMON)

**Question:** Are the helper and game running with different elevation levels?

**How to Check:**
1. Open Task Manager (Ctrl+Shift+Esc)
2. Go to "Details" tab
3. Find processes:
   - `ef-overlay-helper.exe` (or `ef-overlay-tray.exe`)
   - `exefile.exe` (EVE Frontier game client)
4. Look at the "Elevated" column:
   - If helper shows "Yes" and game shows "No" (or vice versa) → **MISMATCH FOUND**

**Why This Causes the Issue:**
Windows uses **session-isolated namespaces** for shared memory:
- Normal processes: `Local\Session\0\EFOverlaySharedState`
- Elevated processes: `Local\Session\0\EFOverlaySharedState` (different session ID)
- If elevation levels don't match, they see different memory spaces

**Solution:**
- **Option A (Recommended):** Run BOTH as normal user (no elevation)
  - Close helper
  - Close game
  - Launch helper normally (NOT as administrator)
  - Launch game normally (NOT as administrator)
  - Click "Start Overlay" in helper

- **Option B:** Run BOTH elevated (not recommended - security risk)
  - Close helper
  - Close game
  - Right-click helper → "Run as administrator"
  - Right-click game launcher → "Run as administrator"
  - Click "Start Overlay"

**Why Option A is Better:**
- EVE Frontier doesn't need admin rights
- Helper only needs elevation during DLL injection (UAC prompt)
- Running everything elevated unnecessarily increases security risk

---

### Step 2: Check Multiple User Sessions

**Question:** Are multiple Windows users logged in simultaneously?

**How to Check:**
1. Press Win+R → type `quser` → press Enter
2. Look for multiple active sessions

**Why This Causes the Issue:**
Each logged-in user has a separate `Local\` namespace. If:
- User A launches helper
- User B (or Remote Desktop session) launches game
- Overlay can't access User A's shared memory

**Solution:**
- Log out all other users except the one running both helper and game
- Ensure helper and game are launched by the **same Windows user account**

---

### Step 3: Verify Shared Memory Creation

**Diagnostic Tool:** Check if helper successfully created shared memory

**How to Check:**
1. Download [Sysinternals Process Explorer](https://docs.microsoft.com/sysinternals/downloads/process-explorer)
2. Run Process Explorer
3. Find `ef-overlay-helper.exe` process
4. Press Ctrl+H to show handles
5. Search for `EFOverlaySharedState`

**Expected Result:**
```
Handle Type: Section
Name: \Sessions\1\BaseNamedObjects\Local\EFOverlaySharedState
```

**If Missing:**
- Helper failed to create shared memory mapping
- Check helper logs: `%LOCALAPPDATA%\EFOverlay\logs\`
- Look for errors like: "Failed to create shared memory mapping"

**If Present:**
- Shared memory exists
- Note the **session ID** (e.g., `\Sessions\1\`)
- Compare to game process session (check `exefile.exe` handles)
- If session IDs differ → Session isolation issue (see Step 1)

---

### Step 4: Check AppContainer Restrictions (Microsoft Store Version)

**Question:** Is this the Microsoft Store MSIX version?

**How to Check:**
1. Open helper installation folder
2. If path contains `WindowsApps` → Microsoft Store version
   - Example: `C:\Program Files\WindowsApps\Ef-Map.EF-MapOverlayHelper_1.0.1.0_x64__...`

**Why This Causes the Issue:**
MSIX apps run in an AppContainer with restricted IPC permissions:
- Helper might create shared memory inside AppContainer namespace
- Game (normal process) can't access AppContainer's `Local\` namespace
- **This is a known limitation** with the current MSIX packaging

**Workaround (Temporary):**
- Use the Debug/Development build instead:
  1. Uninstall Microsoft Store version
  2. Download unsigned development build from GitHub releases
  3. Install test certificate (see `releases/EF-Map-Local-Test.cer`)
  4. Install development MSIX package

**Long-term Fix (Planned):**
- Add elevated COM server for shared memory brokering
- Use alternative IPC mechanism (localhost TCP socket)
- Currently under investigation in decision log

---

### Step 5: Check Anti-Cheat / Security Software

**Question:** Is the user running security software that blocks IPC?

**Common Culprits:**
- Anti-cheat systems (Easy Anti-Cheat, BattleEye, etc.)
- Antivirus real-time protection (Avast, Kaspersky, Norton)
- Windows Defender with strict policies
- Enterprise security policies (corporate machines)

**How to Test:**
1. Temporarily disable antivirus / security software
2. Try starting overlay again
3. If it works → Security software is blocking

**Solution:**
- Add exceptions for:
  - `ef-overlay-helper.exe`
  - `ef-overlay.dll`
  - `ef-overlay-injector.exe`
  - Process: `exefile.exe` (allow DLL injection)
- Check security software documentation for "allow DLL injection" or "allow shared memory access"

**⚠️ Note:** EVE Frontier does NOT use kernel-level anti-cheat (confirmed), but third-party security software might still interfere.

---

### Step 6: Verify DLL Injection Success

**Question:** Did the overlay DLL actually inject into the game?

**How to Check:**
1. Open Process Explorer (see Step 3)
2. Find `exefile.exe` process
3. Press Ctrl+D to show DLLs
4. Search for `ef-overlay.dll`

**Expected Result:**
```
ef-overlay.dll
Path: C:\Users\...\ef-overlay.dll
```

**If Missing:**
- Injection failed (might not see error if helper UI doesn't report it)
- Check helper logs for injection errors
- Verify UAC prompt was accepted (if prompted)
- Ensure game is running in DirectX 12 mode (overlay doesn't support DX11/Vulkan)

**If Present but Still No Connection:**
- DLL injected successfully
- Problem is specifically with shared memory access
- Likely session isolation (back to Step 1)

---

## Quick Diagnostic Checklist

Run through these in order:

1. [ ] Task Manager → Check "Elevated" column for helper and game (must match)
2. [ ] `quser` command → Verify only one user session active
3. [ ] Process Explorer → Verify `EFOverlaySharedState` handle exists in helper
4. [ ] Process Explorer → Verify `ef-overlay.dll` loaded into `exefile.exe`
5. [ ] Check helper logs: `%LOCALAPPDATA%\EFOverlay\logs\ef-overlay-helper.log`
6. [ ] Check overlay logs (if available): Look for OpenFileMapping errors

**Most Common Fix (95% of cases):**
- Close everything
- Launch helper WITHOUT "Run as administrator"
- Launch game WITHOUT "Run as administrator"
- Click "Start Overlay" in helper
- Accept UAC prompt when it appears

---

## Known Issues & Future Fixes

### Issue: MSIX AppContainer Isolation
- **Status:** Under investigation
- **Workaround:** Use development build instead of Store version
- **Long-term Fix:** Planned IPC mechanism redesign

### Issue: Windows 11 Session 0 Isolation
- **Status:** Confirmed on some Windows 11 builds
- **Workaround:** Ensure both processes run in same session
- **Long-term Fix:** Use Global\ namespace with proper security descriptor (requires admin setup)

---

## Collecting Diagnostics for Support

If none of the above steps resolve the issue, collect these details:

**System Information:**
```powershell
# Run in PowerShell
systeminfo | Select-String "OS Name","OS Version","System Type"
```

**Process Information:**
```powershell
# Run in PowerShell (while helper and game are running)
Get-Process ef-overlay-helper,exefile | Select-Object Name,Id,SessionId,SI
```

**Helper Logs:**
```
Location: %LOCALAPPDATA%\EFOverlay\logs\
File: ef-overlay-helper.log (most recent)
```

**UAC Status:**
```powershell
# Check if UAC is enabled
Get-ItemProperty HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Policies\System | Select-Object EnableLUA
```

**Share this information** along with:
- Windows version (Win 10 / Win 11)
- Microsoft Store version or Development build?
- Does helper show UAC prompt when clicking "Start Overlay"?
- Any antivirus / security software installed?

---

## Technical Background (For Developers)

**Shared Memory Architecture:**
- Name: `Local\\EFOverlaySharedState`
- Size: 64 KiB
- Access: Helper writes (PAGE_READWRITE), Overlay reads (FILE_MAP_READ)
- Namespace: Session-local (`Local\` prefix)

**Why Not HTTP?**
- Overlay is a DLL running inside game process
- Game process might not have network permissions
- Shared memory is lower latency (~microseconds vs milliseconds)
- No firewall / network security concerns

**Session Isolation Details:**
- Normal user processes: `\Sessions\<N>\BaseNamedObjects\Local\<name>`
- Elevated processes: Different session ID (even if same user)
- Cross-session access requires `Global\` namespace + SE_CREATE_GLOBAL_NAME privilege

**Why Global\ Namespace Isn't Used:**
- Requires administrator to create (security risk)
- Needs explicit SECURITY_DESCRIPTOR (complex setup)
- `Local\` works fine when processes run at same elevation level

---

**End of Troubleshooting Guide**
