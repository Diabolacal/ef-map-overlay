# Quick Windows Defender Validation Script
# Purpose: Definitively check if Windows Defender is truly active and find hidden antivirus

Write-Host "`n========================================" -ForegroundColor Cyan
Write-Host "   Windows Defender Validation Check" -ForegroundColor Cyan
Write-Host "========================================`n" -ForegroundColor Cyan

# 1. Check Defender Service
Write-Host "[1/4] Checking Windows Defender Service..." -ForegroundColor Yellow
$defenderService = Get-Service WinDefend -ErrorAction SilentlyContinue
if ($defenderService) {
    Write-Host "  Status: $($defenderService.Status)" -ForegroundColor $(if ($defenderService.Status -eq 'Running') { 'Green' } else { 'Red' })
    Write-Host "  Startup: $($defenderService.StartType)`n"
} else {
    Write-Host "  ERROR: Windows Defender service not found!`n" -ForegroundColor Red
}

# 2. Check Real-Time Protection (the critical one)
Write-Host "[2/4] Checking Real-Time Protection..." -ForegroundColor Yellow
try {
    $status = Get-MpComputerStatus -ErrorAction Stop
    Write-Host "  Real-Time Protection: " -NoNewline
    Write-Host $status.RealTimeProtectionEnabled -ForegroundColor $(if ($status.RealTimeProtectionEnabled) { 'Green' } else { 'Red' })
    Write-Host "  Behavior Monitoring: " -NoNewline
    Write-Host $status.BehaviorMonitorEnabled -ForegroundColor $(if ($status.BehaviorMonitorEnabled) { 'Green' } else { 'Red' })
    Write-Host "  On-Access Protection: " -NoNewline
    Write-Host $status.OnAccessProtectionEnabled -ForegroundColor $(if ($status.OnAccessProtectionEnabled) { 'Green' } else { 'Red' })
    Write-Host ""
} catch {
    Write-Host "  ERROR: Cannot read Defender status (likely disabled or third-party AV)!`n" -ForegroundColor Red
}

# 3. Find ALL antivirus products (this is the smoking gun)
Write-Host "[3/4] Scanning for ALL Antivirus Products..." -ForegroundColor Yellow
try {
    $antivirusProducts = Get-CimInstance -Namespace root/SecurityCenter2 -ClassName AntiVirusProduct -ErrorAction Stop
    
    if ($antivirusProducts.Count -eq 0) {
        Write-Host "  WARNING: No antivirus products detected!`n" -ForegroundColor Red
    } else {
        foreach ($av in $antivirusProducts) {
            $state = switch ($av.productState) {
                266240 { "Enabled & Up-to-date" }
                262144 { "DISABLED" }
                393472 { "Enabled & Up-to-date" }
                393216 { "Enabled but Out-of-date" }
                397312 { "Enabled & Up-to-date" }
                397568 { "Enabled & Up-to-date" }
                default { "Unknown state ($($av.productState))" }
            }
            
            $color = if ($av.displayName -match "Defender") { 'Green' } else { 'Yellow' }
            Write-Host "  Found: " -NoNewline
            Write-Host "$($av.displayName)" -ForegroundColor $color -NoNewline
            Write-Host " - $state"
        }
        Write-Host ""
    }
} catch {
    Write-Host "  ERROR: Cannot query Security Center!`n" -ForegroundColor Red
}

# 4. Check for common third-party AV processes (belt and suspenders)
Write-Host "[4/4] Checking for Known Antivirus Processes..." -ForegroundColor Yellow
$knownAVProcesses = @(
    @{Name="Norton"; Process="Norton*"},
    @{Name="McAfee"; Process="McAfee*"},
    @{Name="Kaspersky"; Process="avp*"},
    @{Name="Avast"; Process="Avast*"},
    @{Name="AVG"; Process="AVG*"},
    @{Name="BitDefender"; Process="bdagent*"},
    @{Name="Trend Micro"; Process="TmListen*"},
    @{Name="ESET"; Process="ekrn*"},
    @{Name="Malwarebytes"; Process="mbam*"},
    @{Name="Sophos"; Process="Sophos*"}
)

$foundProcesses = @()
foreach ($av in $knownAVProcesses) {
    $process = Get-Process -Name $av.Process.Replace("*","") -ErrorAction SilentlyContinue
    if ($process) {
        $foundProcesses += $av.Name
        Write-Host "  FOUND: $($av.Name) process running!" -ForegroundColor Red
    }
}

if ($foundProcesses.Count -eq 0) {
    Write-Host "  No known third-party antivirus processes detected.`n" -ForegroundColor Green
} else {
    Write-Host ""
}

# Summary
Write-Host "========================================" -ForegroundColor Cyan
Write-Host "             SUMMARY" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan

if ($defenderService -and $defenderService.Status -eq 'Running' -and 
    $status -and $status.RealTimeProtectionEnabled -eq $true -and
    $antivirusProducts.Count -eq 1 -and $antivirusProducts[0].displayName -match "Defender" -and
    $foundProcesses.Count -eq 0) {
    Write-Host "`n✓ Windows Defender is ACTIVE and PROTECTING" -ForegroundColor Green
    Write-Host "✓ No third-party antivirus detected" -ForegroundColor Green
    Write-Host "`nYour Defender is working correctly." -ForegroundColor White
} else {
    Write-Host "`n✗ ISSUE DETECTED:" -ForegroundColor Red
    
    if ($foundProcesses.Count -gt 0) {
        Write-Host "  Third-party antivirus found: $($foundProcesses -join ', ')" -ForegroundColor Yellow
        Write-Host "  This may be blocking EF-Map overlay injection!" -ForegroundColor Yellow
    }
    
    if ($status -and $status.RealTimeProtectionEnabled -eq $false) {
        Write-Host "  Windows Defender Real-Time Protection is OFF" -ForegroundColor Yellow
    }
    
    if ($antivirusProducts.Count -gt 1) {
        Write-Host "  Multiple antivirus products detected (conflict risk)" -ForegroundColor Yellow
    }
}

Write-Host "`n========================================`n" -ForegroundColor Cyan
Write-Host "Share this entire output with EF-Map support.`n"
