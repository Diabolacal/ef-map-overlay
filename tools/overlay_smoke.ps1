param(
    [string]$GameProcess = "exefile.exe",
    [string]$TargetHost = "127.0.0.1",
    [int]$Port = 38765,
    [switch]$SkipPayload,
    [switch]$DetachHelper
)

$ErrorActionPreference = "Stop"

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).ProviderPath

if (-not $PSBoundParameters.ContainsKey('Port') -and $env:EF_OVERLAY_PORT) {
    $parsedPort = 0
    if ([int]::TryParse($env:EF_OVERLAY_PORT, [ref]$parsedPort)) {
        $Port = $parsedPort
    } else {
        Write-Warning "EF_OVERLAY_PORT='$($env:EF_OVERLAY_PORT)' is not a valid integer; continuing with $Port."
    }
}
$helperExe = Join-Path $repoRoot "build/src/helper/Release/ef-overlay-helper.exe"
$injectorExe = Join-Path $repoRoot "build/src/injector/Release/ef-overlay-injector.exe"
$dllPath = Join-Path $repoRoot "build/src/overlay/Release/ef-overlay.dll"

if (!(Test-Path $helperExe)) {
    throw "Helper executable not found at $helperExe. Build the project first."
}
if (!(Test-Path $injectorExe)) {
    throw "Injector executable not found at $injectorExe. Build the project first."
}
if (!(Test-Path $dllPath)) {
    throw "Overlay DLL not found at $dllPath. Build the project first."
}

Write-Host "Starting overlay smoke test..."
Write-Host "Helper   : $helperExe"
Write-Host "Injector : $injectorExe"
Write-Host "DLL      : $dllPath"

if ($DetachHelper) {
    Write-Host "Launching helper in a separate console window..."
    $helperProcess = Start-Process -FilePath $helperExe -WorkingDirectory (Split-Path $helperExe) -PassThru
} else {
    Write-Host "Launching helper (attached to this window)..."
    $helperProcess = Start-Process -FilePath $helperExe -WorkingDirectory (Split-Path $helperExe) -NoNewWindow -PassThru
}

Start-Sleep -Seconds 1

if (-not $SkipPayload) {
    Write-Host "Posting sample payload to helper..."
    $payload = @{
        version = 1
        notes = "Overlay smoke route"
        route = @(
            @{ system_id = "SMOKE-001"; display_name = "Smoke Test Entry"; distance_ly = 0.0; via_gate = $false }
        )
    } | ConvertTo-Json -Depth 4

    $uri = "http://$TargetHost`:$Port/overlay/state"
    Invoke-WebRequest -UseBasicParsing -Uri $uri -Method Post -ContentType "application/json" -Body $payload | Out-Null
    Write-Host "Payload accepted."
} else {
    Write-Host "Skipping payload submission as requested."
}

Write-Host "Injecting overlay into process '$GameProcess'..."
$injectExitCode = 0
try {
    & $injectorExe $GameProcess $dllPath
    $injectExitCode = $LASTEXITCODE
}
catch {
    Write-Warning "Injector threw an exception: $($_.Exception.Message)"
    $injectExitCode = 1
}

if ($injectExitCode -ne 0) {
    Write-Warning "Injector exited with code $injectExitCode."
}

Write-Host "Smoke test complete. Helper PID: $($helperProcess.Id)."
Write-Host "Close the helper manually when finished (Stop-Process -Id $($helperProcess.Id))."
