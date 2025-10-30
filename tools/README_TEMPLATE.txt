EF Map Overlay Helper v{VERSION}
================================

WHAT IS THIS?
An in-game overlay for EVE Frontier that displays routing information from ef-map.com.

FEATURES
- Route navigation with auto-advance
- Current system tracking (follow mode)
- Mining and combat telemetry
- Proximity scanner (P-SCAN)
- Bookmark creation
- Visit tracking

INSTALLATION (READ CAREFULLY - REQUIRED STEPS)
================================================

STEP 1: Unblock the ZIP file
   Before extracting, right-click the ZIP file → Properties → Check "Unblock" → OK
   This is CRITICAL or Windows will block all files inside.

STEP 2: Extract to permanent location
   Extract to: C:\EFMapHelper\
   DO NOT run from Downloads or Temp folders

STEP 3: Add Windows Defender exclusion (REQUIRED)
   a. Open Windows Security
   b. Virus and threat protection → Manage settings
   c. Scroll to "Exclusions" → Add or remove exclusions
   d. Add an exclusion → Folder → Select C:\EFMapHelper\

STEP 4: Launch the helper
   Right-click "launch_helper.ps1" → Run with PowerShell
   OR double-click "ef-overlay-helper.exe"

STEP 5: Use in-game
   1. Open EVE Frontier
   2. Visit ef-map.com and calculate a route
   3. Press F8 in-game to toggle overlay

TROUBLESHOOTING
===============

Q: Windows Defender deleted the EXE
A: You must add the folder exclusion BEFORE extracting.
   Restore from quarantine: Windows Security → Protection history → Restore

Q: PowerShell won't run the script  
A: Open PowerShell as Admin:
   Set-ExecutionPolicy -Scope CurrentUser RemoteSigned

Q: Overlay doesn't appear
A: 1. Check system tray for EF icon
   2. Calculate a route on ef-map.com first
   3. Press F8 in EVE Frontier

WHY DOES WINDOWS BLOCK THIS?
=============================
This tool uses process injection to display an overlay in EVE Frontier.
Windows flags this as suspicious, but it's safe - the code is open source.

Source code: https://github.com/Diabolacal/EF-Map
You can audit it yourself.

PRIVACY
=======
All data stays on your PC. Nothing is uploaded to the cloud.

SYSTEM REQUIREMENTS
===================
- Windows 10/11 (64-bit)
- EVE Frontier
- DirectX 12 GPU

UNINSTALL
=========
1. Close helper (right-click tray icon → Quit)
2. Delete C:\EFMapHelper\
3. Remove Defender exclusion
4. Delete: %LOCALAPPDATA%\EFOverlay\

SUPPORT
=======
GitHub: https://github.com/Diabolacal/EF-Map/issues
Website: https://ef-map.com

VERSION {VERSION}
