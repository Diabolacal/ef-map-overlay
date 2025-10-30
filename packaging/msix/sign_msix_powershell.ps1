# Sign MSIX using PowerShell (Alternative Method)
# Works when SignTool fails with self-signed certificates

param(
    [string]$MsixPath = "C:\ef-map-overlay\releases\EFMapHelper-v1.0.0.msix"
)

$ErrorActionPreference = "Stop"

$OutputDir = Split-Path $MsixPath -Parent
$SignedMsixPath = Join-Path $OutputDir "EFMapHelper-v1.0.0-SIGNED-LOCAL-TEST.msix"
$CertName = "EF-Map-Local-Test"

Write-Host "Looking for existing certificate..." -ForegroundColor Cyan
$Cert = Get-ChildItem Cert:\CurrentUser\My | Where-Object { $_.Subject -like "*$CertName*" } | Select-Object -First 1

if (-not $Cert) {
    Write-Host "Certificate not found. Run sign_for_local_testing.ps1 first!" -ForegroundColor Red
    exit 1
}

Write-Host "Found certificate: $($Cert.Thumbprint)" -ForegroundColor Green
Write-Host ""

# Copy unsigned MSIX
Copy-Item $MsixPath -Destination $SignedMsixPath -Force

# Sign using PowerShell cmdlet instead of SignTool
Write-Host "Signing MSIX with PowerShell..."
try {
    Set-AuthenticodeSignature -FilePath $SignedMsixPath -Certificate $Cert -TimestampServer "http://timestamp.digicert.com" -HashAlgorithm SHA256
    Write-Host "✅ MSIX signed successfully!" -ForegroundColor Green
} catch {
    Write-Host "❌ Signing failed: $_" -ForegroundColor Red
    Write-Host ""
    Write-Host "Trying without timestamp..." -ForegroundColor Yellow
    Set-AuthenticodeSignature -FilePath $SignedMsixPath -Certificate $Cert -HashAlgorithm SHA256
}

Write-Host ""
Write-Host "Verifying signature..."
$Sig = Get-AuthenticodeSignature -FilePath $SignedMsixPath
Write-Host "Status: $($Sig.Status)" -ForegroundColor $(if ($Sig.Status -eq 'Valid') { 'Green' } else { 'Yellow' })

Write-Host ""
Write-Host "✅ Signed MSIX ready: $SignedMsixPath" -ForegroundColor Green
