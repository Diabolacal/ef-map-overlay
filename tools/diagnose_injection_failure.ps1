#Requires -Version 5.1
<#
.SYNOPSIS
    EF-Map Overlay - Injection Failure Diagnostic Tool

.DESCRIPTION
    Collects comprehensive diagnostics when overlay injection fails but helper
    shows connected status. Designed for low-friction user execution via Discord
    distribution. Outputs structured report for developer analysis.

.PARAMETER OutputFile
    Path to save diagnostic report (default: desktop)

.PARAMETER IncludeLogs
    Include full helper log file contents (increases report size)

.EXAMPLE
    .\diagnose_injection_failure.ps1
    # Runs diagnostics and saves report to desktop

.EXAMPLE
    .\diagnose_injection_failure.ps1 -IncludeLogs
    # Includes full helper logs in report

.NOTES
    Version: 1.0.0
    Last Updated: 2025-11-06
    Requires: Windows 10+, PowerShell 5.1+
    Must run while helper and game are both running for accurate diagnostics
#>

[CmdletBinding()]
param(
    [string]$OutputFile = "$env:USERPROFILE\Desktop\ef-overlay-diagnostics-$(Get-Date -Format 'yyyyMMdd-HHmmss').txt",
    [switch]$IncludeLogs
)

# Banner
Write-Host @"
========================================
  EF-Map Overlay Diagnostic Tool
========================================
Collecting system information...

"@ -ForegroundColor Cyan

$Report = @()
$Issues = @()

# Helper function to add section to report
function Add-Section {
    param([string]$Title, [string]$Content)
    $script:Report += "`n========================================`n"
    $script:Report += "  $Title`n"
    $script:Report += "========================================`n"
    $script:Report += $Content
}

# Helper function to add issue
function Add-Issue {
    param([string]$Severity, [string]$Message)
    $script:Issues += "[${Severity}] $Message"
}

# ============================================
# 1. SYSTEM INFORMATION
# ============================================
Write-Host "Collecting system information..." -ForegroundColor Yellow

try {
    $os = Get-CimInstance Win32_OperatingSystem
    $sysInfo = @"
OS Name:        $($os.Caption)
OS Version:     $($os.Version)
OS Build:       $($os.BuildNumber)
Architecture:   $([System.Environment]::Is64BitOperatingSystem)
System Type:    $($os.OSArchitecture)
Collected At:   $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')
"@
    Add-Section "System Information" $sysInfo
} catch {
    Add-Section "System Information" "ERROR: $_"
    Add-Issue "ERROR" "Failed to collect system information"
}

# ============================================
# 2. UAC STATUS
# ============================================
Write-Host "Checking UAC configuration..." -ForegroundColor Yellow

try {
    $uac = Get-ItemProperty HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Policies\System -ErrorAction Stop
    $uacEnabled = if ($uac.EnableLUA -eq 1) { "Enabled" } else { "Disabled" }
    $uacInfo = @"
UAC Status:     $uacEnabled
ConsentPrompt:  $($uac.ConsentPromptBehaviorAdmin)
"@
    Add-Section "UAC Configuration" $uacInfo
    
    if ($uac.EnableLUA -eq 0) {
        Add-Issue "WARNING" "UAC is disabled - may affect overlay injection"
    }
} catch {
    Add-Section "UAC Configuration" "ERROR: $_"
    Add-Issue "ERROR" "Failed to check UAC status"
}

# ============================================
# 3. USER SESSION INFORMATION
# ============================================
Write-Host "Checking user sessions..." -ForegroundColor Yellow

try {
    $sessions = quser 2>&1
    if ($LASTEXITCODE -eq 0) {
        Add-Section "Active User Sessions" $sessions
        
        # Parse session count
        $sessionLines = ($sessions | Select-Object -Skip 1 | Measure-Object).Count
        if ($sessionLines -gt 1) {
            Add-Issue "WARNING" "Multiple user sessions detected ($sessionLines active) - this can cause shared memory isolation"
        }
    } else {
        Add-Section "Active User Sessions" "Only one session active (current user)"
    }
} catch {
    Add-Section "Active User Sessions" "ERROR: $_"
}

# ============================================
# 4. PROCESS INFORMATION
# ============================================
Write-Host "Checking helper and game processes..." -ForegroundColor Yellow

$helperProc = $null
$gameProc = $null
$helperNames = @("ef-overlay-helper", "ef-overlay-tray", "EF-MapOverlayHelper")
$gameName = "exefile"

try {
    # Find helper process
    foreach ($name in $helperNames) {
        $proc = Get-Process -Name $name -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($proc) {
            $helperProc = $proc
            break
        }
    }
    
    # Find game process
    $gameProc = Get-Process -Name $gameName -ErrorAction SilentlyContinue | Select-Object -First 1
    
    if ($helperProc -or $gameProc) {
        # Get detailed process info
        $procDetails = Get-Process -Id (@($helperProc.Id, $gameProc.Id) | Where-Object { $_ }) -ErrorAction SilentlyContinue | 
            Select-Object Name, Id, SessionId, SI, StartTime, 
                @{N='Path';E={$_.Path}},
                @{N='WorkingSet(MB)';E={[math]::Round($_.WorkingSet64 / 1MB, 2)}},
                @{N='Elevated';E={
                    try {
                        $proc = Get-Process -Id $_.Id
                        $token = [System.Diagnostics.Process]::GetProcessById($_.Id)
                        $principal = New-Object Security.Principal.WindowsPrincipal([Security.Principal.WindowsIdentity]::GetCurrent())
                        $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
                    } catch {
                        "Unknown"
                    }
                }}
        
        $procInfo = $procDetails | Format-List | Out-String
        Add-Section "Process Details" $procInfo
        
        # Check for issues
        if (-not $helperProc) {
            Add-Issue "CRITICAL" "Helper process not found - overlay cannot inject"
        }
        
        if (-not $gameProc) {
            Add-Issue "WARNING" "Game process (exefile.exe) not found - unable to verify injection"
        }
        
        if ($helperProc -and $gameProc) {
            # Check session ID mismatch
            if ($helperProc.SessionId -ne $gameProc.SessionId) {
                Add-Issue "CRITICAL" "Session ID mismatch: Helper ($($helperProc.SessionId)) vs Game ($($gameProc.SessionId)) - shared memory isolation"
            }
            
            # Check elevation mismatch
            $helperDetails = $procDetails | Where-Object { $_.Id -eq $helperProc.Id }
            $gameDetails = $procDetails | Where-Object { $_.Id -eq $gameProc.Id }
            
            if ($helperDetails.Elevated -ne $gameDetails.Elevated -and 
                $helperDetails.Elevated -ne "Unknown" -and 
                $gameDetails.Elevated -ne "Unknown") {
                Add-Issue "CRITICAL" "Elevation mismatch: Helper (Elevated=$($helperDetails.Elevated)) vs Game (Elevated=$($gameDetails.Elevated)) - most common injection failure"
            }
        }
    } else {
        Add-Section "Process Details" "CRITICAL: Neither helper nor game process found running"
        Add-Issue "CRITICAL" "No helper or game process detected - ensure both are running"
    }
} catch {
    Add-Section "Process Details" "ERROR: $_"
    Add-Issue "ERROR" "Failed to collect process information"
}

# ============================================
# 5. HELPER INSTALLATION DETECTION
# ============================================
Write-Host "Detecting helper installation type..." -ForegroundColor Yellow

try {
    $installType = "Unknown"
    $installPath = "Not found"
    
    if ($helperProc) {
        $installPath = $helperProc.Path
        
        if ($installPath -like "*WindowsApps*") {
            $installType = "Microsoft Store (MSIX)"
            Add-Issue "WARNING" "Microsoft Store version detected - AppContainer restrictions may prevent injection"
        } elseif ($installPath -like "*Program Files*") {
            $installType = "Sideloaded MSIX (Development)"
        } else {
            $installType = "Standalone Build"
        }
    }
    
    $installInfo = @"
Installation Type: $installType
Helper Path:       $installPath
"@
    Add-Section "Helper Installation" $installInfo
} catch {
    Add-Section "Helper Installation" "ERROR: $_"
}

# ============================================
# 6. DLL INJECTION STATUS
# ============================================
Write-Host "Checking DLL injection status..." -ForegroundColor Yellow

try {
    if ($gameProc) {
        # Try to enumerate loaded modules (requires elevated PowerShell or Process Explorer)
        $dllFound = $false
        $moduleInfo = "Checking loaded modules in game process...`n"
        
        try {
            # Attempt to query process modules
            $modules = Get-Process -Id $gameProc.Id -Module -ErrorAction SilentlyContinue
            
            if ($modules) {
                $overlayDll = $modules | Where-Object { $_.ModuleName -like "*ef-overlay*.dll" }
                
                if ($overlayDll) {
                    $dllFound = $true
                    $moduleInfo += "`nOVERLAY DLL FOUND:`n"
                    $moduleInfo += $overlayDll | Format-List ModuleName, FileName, Size | Out-String
                } else {
                    $moduleInfo += "`nOverlay DLL NOT found in process modules`n"
                    $moduleInfo += "Loaded modules: $($modules.Count) total`n"
                    Add-Issue "CRITICAL" "Overlay DLL not loaded into game process - injection failed"
                }
            } else {
                $moduleInfo += "`nUnable to enumerate process modules (may require elevation)`n"
                $moduleInfo += "Recommendation: Use Process Explorer (Sysinternals) to verify DLL injection`n"
            }
        } catch {
            $moduleInfo += "`nERROR querying modules: $_`n"
            $moduleInfo += "Recommendation: Run script as Administrator or use Process Explorer`n"
        }
        
        Add-Section "DLL Injection Status" $moduleInfo
    } else {
        Add-Section "DLL Injection Status" "Game process not running - cannot verify DLL status"
    }
} catch {
    Add-Section "DLL Injection Status" "ERROR: $_"
}

# ============================================
# 7. HELPER LOGS
# ============================================
Write-Host "Collecting helper logs..." -ForegroundColor Yellow

try {
    $logPath = "$env:LOCALAPPDATA\EFOverlay\logs"
    $logFiles = Get-ChildItem -Path $logPath -Filter "*.log" -ErrorAction SilentlyContinue | 
        Sort-Object LastWriteTime -Descending
    
    if ($logFiles) {
        $latestLog = $logFiles | Select-Object -First 1
        $logInfo = @"
Log Directory:  $logPath
Latest Log:     $($latestLog.Name)
Last Modified:  $($latestLog.LastWriteTime)
Size:           $([math]::Round($latestLog.Length / 1KB, 2)) KB
"@
        
        if ($IncludeLogs) {
            $logInfo += "`n`n--- LOG CONTENTS (Last 200 lines) ---`n"
            $logContent = Get-Content -Path $latestLog.FullName -Tail 200 -ErrorAction SilentlyContinue
            $logInfo += $logContent -join "`n"
        } else {
            $logInfo += "`n`nNote: Use -IncludeLogs parameter to include full log contents"
            
            # Extract key errors/warnings
            $logContent = Get-Content -Path $latestLog.FullName -Tail 100 -ErrorAction SilentlyContinue
            $errors = $logContent | Where-Object { $_ -match "\[error\]|\[critical\]" }
            $warnings = $logContent | Where-Object { $_ -match "\[warn\]" }
            
            if ($errors) {
                $logInfo += "`n`n--- Recent Errors (Last 10) ---`n"
                $logInfo += ($errors | Select-Object -Last 10) -join "`n"
            }
            
            if ($warnings) {
                $logInfo += "`n`n--- Recent Warnings (Last 10) ---`n"
                $logInfo += ($warnings | Select-Object -Last 10) -join "`n"
            }
        }
        
        Add-Section "Helper Logs" $logInfo
        
        # Check for common error patterns
        $fullLog = Get-Content -Path $latestLog.FullName -ErrorAction SilentlyContinue
        if ($fullLog -match "Failed to create shared memory") {
            Add-Issue "CRITICAL" "Helper failed to create shared memory mapping"
        }
        if ($fullLog -match "Injection failed") {
            Add-Issue "CRITICAL" "DLL injection explicitly failed - check log for details"
        }
        if ($fullLog -match "Access denied") {
            Add-Issue "CRITICAL" "Access denied error detected - possible permission issue"
        }
    } else {
        Add-Section "Helper Logs" "No log files found at $logPath"
        Add-Issue "WARNING" "Helper logs not found - helper may not be configured correctly"
    }
} catch {
    Add-Section "Helper Logs" "ERROR: $_"
}

# ============================================
# 8. ANTIVIRUS / SECURITY SOFTWARE
# ============================================
Write-Host "Checking security software..." -ForegroundColor Yellow

try {
    $avInfo = ""
    
    # Windows Defender status
    try {
        $defender = Get-MpComputerStatus -ErrorAction SilentlyContinue
        if ($defender) {
            $avInfo += @"
Windows Defender:
  Real-time Protection: $($defender.RealTimeProtectionEnabled)
  Tamper Protection:    $($defender.IsTamperProtected)
  Last Scan:            $($defender.QuickScanEndTime)

"@
        }
    } catch {
        $avInfo += "Windows Defender: Unable to query status`n"
    }
    
    # Check for common antivirus products via installed applications
    $avProducts = Get-CimInstance -ClassName Win32_Product -ErrorAction SilentlyContinue | 
        Where-Object { $_.Name -match "Avast|Norton|McAfee|Kaspersky|Bitdefender|AVG|Malwarebytes|ESET" } |
        Select-Object Name, Version
    
    if ($avProducts) {
        $avInfo += "`nDetected Security Software:`n"
        $avInfo += $avProducts | Format-Table -AutoSize | Out-String
        Add-Issue "WARNING" "Third-party security software detected - may block DLL injection"
    } else {
        $avInfo += "`nNo third-party security software detected via installed programs`n"
    }
    
    Add-Section "Security Software" $avInfo
} catch {
    Add-Section "Security Software" "ERROR: $_"
}

# ============================================
# 9. HELPER API STATUS
# ============================================
Write-Host "Testing helper API connectivity..." -ForegroundColor Yellow

try {
    $apiResults = ""
    $apiBaseUrl = "http://127.0.0.1:38765"
    
    # Test health endpoint
    try {
        $health = Invoke-RestMethod -Uri "$apiBaseUrl/health" -TimeoutSec 5 -ErrorAction Stop
        $apiResults += "Health Endpoint: OK`n"
        $apiResults += $health | ConvertTo-Json -Depth 2
        $apiResults += "`n"
    } catch {
        $apiResults += "Health Endpoint: FAILED - $($_.Exception.Message)`n"
        Add-Issue "CRITICAL" "Helper HTTP API not responding - helper may not be running"
    }
    
    # Test status endpoint
    try {
        $status = Invoke-RestMethod -Uri "$apiBaseUrl/api/status" -TimeoutSec 5 -ErrorAction Stop
        $apiResults += "`nStatus Endpoint: OK`n"
        $apiResults += $status | ConvertTo-Json -Depth 3
        
        # Check overlay connection status
        if ($status.overlay_connected -eq $false) {
            Add-Issue "CRITICAL" "Helper reports overlay NOT connected"
        }
    } catch {
        $apiResults += "`nStatus Endpoint: FAILED - $($_.Exception.Message)`n"
    }
    
    Add-Section "Helper API Status" $apiResults
} catch {
    Add-Section "Helper API Status" "ERROR: $_"
}

# ============================================
# 10. SHARED MEMORY DIAGNOSTIC
# ============================================
Write-Host "Checking shared memory status..." -ForegroundColor Yellow

$shmemInfo = @"
Note: Verifying shared memory existence requires Process Explorer (Sysinternals)
or elevated PowerShell with handle enumeration tools.

Expected shared memory name: Local\EFOverlaySharedState
Expected location: \Sessions\<N>\BaseNamedObjects\Local\EFOverlaySharedState

Manual verification steps:
1. Download Process Explorer from Microsoft Sysinternals
2. Find ef-overlay-helper.exe process
3. Press Ctrl+H to show handles
4. Search for "EFOverlaySharedState"
5. Note the session ID in the path
6. Compare to game process (exefile.exe) session ID

If session IDs don't match: Elevation/session isolation issue (see Process Details section)
"@

Add-Section "Shared Memory Diagnostic" $shmemInfo

# ============================================
# GENERATE SUMMARY
# ============================================
Write-Host "`nGenerating summary..." -ForegroundColor Yellow

$summary = @"
========================================
  DIAGNOSTIC SUMMARY
========================================
Total Issues Found: $($Issues.Count)

"@

if ($Issues.Count -eq 0) {
    $summary += "No obvious issues detected. If overlay still fails:`n"
    $summary += "- Check helper logs for detailed error messages`n"
    $summary += "- Verify DirectX 12 mode (overlay doesn't support DX11/Vulkan)`n"
    $summary += "- Try closing all processes and restarting both helper and game`n"
} else {
    $summary += "DETECTED ISSUES:`n"
    $summary += ($Issues -join "`n")
    $summary += "`n`n"
    
    # Add recommendations based on issues
    $summary += "RECOMMENDATIONS:`n"
    
    if ($Issues -match "Elevation mismatch") {
        $summary += @"
1. MOST LIKELY FIX - Elevation Mismatch:
   - Close helper completely
   - Close game completely
   - Launch helper WITHOUT "Run as administrator"
   - Launch game WITHOUT "Run as administrator"
   - Click "Start Overlay" in helper UI
   - Accept UAC prompt if it appears

"@
    }
    
    if ($Issues -match "Session ID mismatch") {
        $summary += @"
2. Session Isolation Issue:
   - Ensure no other Windows users are logged in
   - Log out of all Remote Desktop sessions
   - Both helper and game must run under the same user account

"@
    }
    
    if ($Issues -match "Microsoft Store version") {
        $summary += @"
3. Microsoft Store AppContainer Restriction:
   - Known limitation with MSIX packaging
   - Consider using development build instead
   - Contact developer for alternative installation package

"@
    }
    
    if ($Issues -match "DLL not loaded") {
        $summary += @"
4. DLL Injection Failed:
   - Check helper logs for specific injection errors
   - Verify game is running in DirectX 12 mode
   - Ensure antivirus isn't blocking injection
   - Try running helper as Administrator

"@
    }
    
    if ($Issues -match "security software") {
        $summary += @"
5. Security Software Interference:
   - Temporarily disable antivirus real-time protection
   - Add exceptions for:
     * ef-overlay-helper.exe
     * ef-overlay.dll
     * ef-overlay-injector.exe
     * exefile.exe (allow DLL injection)

"@
    }
}

$summary += @"

========================================
  NEXT STEPS
========================================
1. Save this report file
2. Send it to the developer via Discord
3. Include description of what happens when you click "Start Overlay"
4. Mention if you see any UAC prompts
5. Note if helper tray icon shows any error messages

Report saved to: $OutputFile
========================================
"@

# Add summary at the beginning of report
$fullReport = $summary + "`n`n" + ($Report -join "")

# ============================================
# SAVE REPORT
# ============================================
try {
    $fullReport | Out-File -FilePath $OutputFile -Encoding UTF8
    Write-Host "`nDiagnostic report saved to:" -ForegroundColor Green
    Write-Host $OutputFile -ForegroundColor Cyan
    
    # Display summary on screen
    Write-Host "`n$summary" -ForegroundColor White
    
    # Offer to open file
    Write-Host "`nOpen report file? (Y/N): " -NoNewline -ForegroundColor Yellow
    $response = Read-Host
    if ($response -eq 'Y' -or $response -eq 'y') {
        Start-Process notepad.exe -ArgumentList $OutputFile
    }
    
} catch {
    Write-Host "`nERROR: Failed to save report: $_" -ForegroundColor Red
    Write-Host "`nDumping report to console instead:`n" -ForegroundColor Yellow
    Write-Host $fullReport
}

Write-Host "`nDiagnostic collection complete!" -ForegroundColor Green
