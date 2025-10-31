# EF-Map Overlay Diagnostic Collection Script
# 
# Purpose: Safely collect technical diagnostics without exposing personal data
# Usage: Right-click → "Run with PowerShell" OR run in PowerShell window
# Output: Creates "EF-Map-Diagnostics.txt" on Desktop

Write-Host "========================================" -ForegroundColor Cyan
Write-Host "EF-Map Overlay Diagnostic Collection" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""
Write-Host "This script collects technical information to help diagnose overlay issues." -ForegroundColor Yellow
Write-Host "NO personal data is collected (no usernames, file contents, or screenshots)." -ForegroundColor Yellow
Write-Host ""

$outputPath = [System.IO.Path]::Combine([Environment]::GetFolderPath('Desktop'), 'EF-Map-Diagnostics.txt')

# Create output file
$output = @()

# Header
$output += "=== EF-Map Overlay Diagnostic Report ==="
$output += "Generated: $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')"
$output += ""

# 1. Windows Version
Write-Host "[1/9] Checking Windows version..." -ForegroundColor Green
try {
    $os = Get-ItemProperty "HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion"
    $output += "=== Windows Information ==="
    $output += "Product Name: $($os.ProductName)"
    $output += "Build Number: $($os.CurrentBuild)"
    $output += "Display Version: $($os.DisplayVersion)"
    $output += "Edition: $($os.EditionID)"
    $output += ""
} catch {
    $output += "ERROR: Could not retrieve Windows version"
    $output += ""
}

# 2. Process Information (Helper & Game)
Write-Host "[2/9] Checking running processes..." -ForegroundColor Green
try {
    $processes = Get-Process ef-overlay-tray,ef-overlay-helper,exefile -ErrorAction SilentlyContinue
    
    if ($processes) {
        $output += "=== Running Processes ==="
        foreach ($proc in $processes) {
            $output += "Process: $($proc.Name)"
            $output += "  PID: $($proc.Id)"
            $output += "  Session ID: $($proc.SessionId)"
            
            # Check if elevated (requires admin rights to check other processes)
            try {
                $token = [System.Diagnostics.Process]::GetProcessById($proc.Id).Handle
                $elevated = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
                $output += "  Elevated: $elevated"
            } catch {
                $output += "  Elevated: Unable to determine (requires admin)"
            }
            
            $output += ""
        }
    } else {
        $output += "=== Running Processes ==="
        $output += "NOTICE: No helper or game processes found running"
        $output += "(This is normal if you haven't started them yet)"
        $output += ""
    }
} catch {
    $output += "ERROR: Could not check processes"
    $output += ""
}

# 3. User Sessions
Write-Host "[3/9] Checking user sessions..." -ForegroundColor Green
try {
    $output += "=== Active User Sessions ==="
    $sessions = query user 2>&1
    if ($LASTEXITCODE -eq 0) {
        $output += $sessions | Out-String
    } else {
        $output += "Only one user session active (normal)"
    }
    $output += ""
} catch {
    $output += "ERROR: Could not query user sessions"
    $output += ""
}

# 4. Helper Installation Path & DLL Location
Write-Host "[4/9] Checking helper installation..." -ForegroundColor Green
try {
    $output += "=== Helper Installation ==="
    
    # Check Microsoft Store install
    $storeApp = Get-Item "C:\Program Files\WindowsApps\*EF-Map*\*\ef-overlay-tray.exe" -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($storeApp) {
        $output += "Installation Type: Microsoft Store (MSIX)"
        $output += "Helper Path: $($storeApp.DirectoryName)"
        
        # Check for DLL
        $dll = Get-Item "$($storeApp.DirectoryName)\ef-overlay.dll" -ErrorAction SilentlyContinue
        if ($dll) {
            $output += "DLL Found: YES"
            $output += "DLL Size: $($dll.Length) bytes"
            $output += "DLL Date: $($dll.LastWriteTime)"
        } else {
            $output += "DLL Found: NO (PROBLEM!)"
        }
    } else {
        $output += "Installation Type: Not found in WindowsApps (custom install or not installed)"
    }
    $output += ""
} catch {
    $output += "ERROR: Could not check installation"
    $output += ""
}

# 5. Windows Defender Status
Write-Host "[5/9] Checking Windows Defender..." -ForegroundColor Green
try {
    $output += "=== Windows Defender Status ==="
    $defender = Get-MpComputerStatus -ErrorAction SilentlyContinue
    if ($defender) {
        $output += "Antivirus Enabled: $($defender.AntivirusEnabled)"
        $output += "Real-Time Protection: $($defender.RealTimeProtectionEnabled)"
        $output += "Behavior Monitoring: $($defender.BehaviorMonitorEnabled)"
        $output += "IOAV Protection: $($defender.IoavProtectionEnabled)"
    } else {
        $output += "Windows Defender: Not available or disabled"
    }
    $output += ""
} catch {
    $output += "Windows Defender: Unable to query status"
    $output += ""
}

# 6. UAC Settings
Write-Host "[6/9] Checking UAC settings..." -ForegroundColor Green
try {
    $output += "=== User Account Control (UAC) ==="
    $uac = Get-ItemProperty "HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Policies\System" -ErrorAction SilentlyContinue
    if ($uac) {
        $uacEnabled = $uac.EnableLUA
        $output += "UAC Enabled: $uacEnabled"
        $output += "Consent Prompt Behavior: $($uac.ConsentPromptBehaviorAdmin)"
    } else {
        $output += "UAC: Unable to determine"
    }
    $output += ""
} catch {
    $output += "ERROR: Could not check UAC settings"
    $output += ""
}

# 7. Network/Firewall (Basic Check)
Write-Host "[7/9] Checking localhost connectivity..." -ForegroundColor Green
try {
    $output += "=== Network Connectivity ==="
    $httpTest = Test-NetConnection -ComputerName localhost -Port 38765 -InformationLevel Quiet -WarningAction SilentlyContinue
    $output += "Helper API Port (38765) Open: $httpTest"
    $output += ""
} catch {
    $output += "ERROR: Could not test network connectivity"
    $output += ""
}

# 8. Third-Party Software Detection (Common Issues)
Write-Host "[8/9] Checking for common conflicting software..." -ForegroundColor Green
try {
    $output += "=== Potentially Conflicting Software ==="
    
    $antivirusApps = @(
        "*Norton*", "*McAfee*", "*Kaspersky*", "*Avast*", "*AVG*",
        "*Bitdefender*", "*Malwarebytes*", "*Webroot*", "*ESET*"
    )
    
    $vmApps = @(
        "*VMware*", "*VirtualBox*", "*Parallels*"
    )
    
    $found = @()
    
    # Check installed programs
    $programs = Get-ItemProperty "HKLM:\Software\Microsoft\Windows\CurrentVersion\Uninstall\*",
                                   "HKLM:\Software\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\*" -ErrorAction SilentlyContinue |
                Select-Object DisplayName
    
    foreach ($av in $antivirusApps) {
        $match = $programs | Where-Object { $_.DisplayName -like $av }
        if ($match) {
            $found += "Antivirus: $($match.DisplayName)"
        }
    }
    
    foreach ($vm in $vmApps) {
        $match = $programs | Where-Object { $_.DisplayName -like $vm }
        if ($match) {
            $found += "Virtualization: $($match.DisplayName)"
        }
    }
    
    if ($found.Count -gt 0) {
        $output += $found
        $output += ""
        $output += "NOTE: These programs may interfere with DLL injection."
    } else {
        $output += "No common conflicting software detected"
    }
    $output += ""
} catch {
    $output += "ERROR: Could not scan for conflicting software"
    $output += ""
}

# 9. Current User Context
Write-Host "[9/9] Checking user context..." -ForegroundColor Green
try {
    $output += "=== User Context ==="
    $currentUser = [Security.Principal.WindowsIdentity]::GetCurrent()
    $output += "Running as Administrator: $(([Security.Principal.WindowsPrincipal]$currentUser).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator))"
    $output += "User Account Type: $($currentUser.AuthenticationType)"
    $output += ""
} catch {
    $output += "ERROR: Could not determine user context"
    $output += ""
}

# Additional Notes Section
$output += "=== Additional Information Needed ==="
$output += "Please answer these questions:"
$output += "1. Do you have any antivirus software besides Windows Defender? (Yes/No, which one?)"
$output += "2. Is this a work/school computer managed by an organization? (Yes/No)"
$output += "3. Are you using a VPN when trying to start the overlay? (Yes/No)"
$output += "4. What error message do you see when trying to start overlay?"
$output += "5. Does the helper tray icon show 'Connected' in green? (Yes/No)"
$output += ""

# Footer
$output += "=== End of Report ==="
$output += ""
$output += "Please share this file with EF-Map support."
$output += "This report contains NO personal data (no usernames, passwords, or file contents)."

# Write to file
$output | Out-File -FilePath $outputPath -Encoding UTF8

Write-Host ""
Write-Host "========================================" -ForegroundColor Green
Write-Host "Diagnostic collection complete!" -ForegroundColor Green
Write-Host "========================================" -ForegroundColor Green
Write-Host ""
Write-Host "File saved to: $outputPath" -ForegroundColor Cyan
Write-Host ""
Write-Host "Please answer the 5 questions at the end of the file," -ForegroundColor Yellow
Write-Host "then share the file with EF-Map support." -ForegroundColor Yellow
Write-Host ""
Write-Host "Opening file now..." -ForegroundColor Gray

# Open the file
Start-Process notepad.exe -ArgumentList $outputPath

Write-Host ""
Write-Host "Press any key to exit..." -ForegroundColor Gray
$null = $Host.UI.RawUI.ReadKey("NoEcho,IncludeKeyDown")
