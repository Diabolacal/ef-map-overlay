# Sign MSIX for Local Testing
# Creates a self-signed certificate and signs the MSIX package for local installation testing
# This is ONLY for testing - do NOT upload this signed version to Microsoft Store!

param(
    [string]$MsixPath = "C:\ef-map-overlay\releases\EFMapHelper-v1.0.0.msix",
    [string]$CertName = "EF-Map-Local-Test"
)

$ErrorActionPreference = "Stop"

# Paths
$SignTool = "C:\Program Files (x86)\Windows Kits\10\bin\10.0.19041.0\x64\signtool.exe"
$CertUtil = "certutil.exe"
$OutputDir = Split-Path $MsixPath -Parent
$SignedMsixPath = Join-Path $OutputDir "EFMapHelper-v1.0.0-SIGNED-LOCAL-TEST.msix"

Write-Host "Creating self-signed certificate for local testing..." -ForegroundColor Cyan
Write-Host ""

# Check if certificate already exists
$ExistingCert = Get-ChildItem Cert:\CurrentUser\My | Where-Object { $_.Subject -like "*$CertName*" } | Select-Object -First 1

if ($ExistingCert) {
    Write-Host "Found existing certificate: $($ExistingCert.Thumbprint)" -ForegroundColor Yellow
    $Cert = $ExistingCert
} else {
    Write-Host "Creating new self-signed certificate..."
    
    # Create self-signed certificate
    $Cert = New-SelfSignedCertificate `
        -Type Custom `
        -Subject "CN=$CertName" `
        -KeyUsage DigitalSignature `
        -FriendlyName "EF Map Overlay Local Testing Certificate" `
        -CertStoreLocation "Cert:\CurrentUser\My" `
        -TextExtension @("2.5.29.37={text}1.3.6.1.5.5.7.3.3", "2.5.29.19={text}")
    
    Write-Host "✅ Certificate created: $($Cert.Thumbprint)" -ForegroundColor Green
}

Write-Host ""
Write-Host "Exporting certificate for installation..."

# Export certificate to file
$CertPath = Join-Path $OutputDir "$CertName.cer"
Export-Certificate -Cert $Cert -FilePath $CertPath -Type CERT | Out-Null
Write-Host "✅ Certificate exported to: $CertPath" -ForegroundColor Green

Write-Host ""
Write-Host "Copying MSIX package..."
Copy-Item $MsixPath -Destination $SignedMsixPath -Force
Write-Host "✅ Copied to: $SignedMsixPath" -ForegroundColor Green

Write-Host ""
Write-Host "Signing MSIX package with certificate thumbprint..."
& $SignTool sign /fd SHA256 /sha1 $($Cert.Thumbprint) /tr http://timestamp.digicert.com /td SHA256 $SignedMsixPath

if ($LASTEXITCODE -ne 0) {
    Write-Host "❌ Signing failed!" -ForegroundColor Red
    Write-Host "This is expected if you don't have code signing permissions." -ForegroundColor Yellow
    Write-Host "The MSIX is still created - install certificate first, then try MSIX install." -ForegroundColor Yellow
}

Write-Host "✅ MSIX signed successfully!" -ForegroundColor Green

Write-Host ""
Write-Host "==================================" -ForegroundColor Green
Write-Host "LOCAL TEST PACKAGE READY" -ForegroundColor Green
Write-Host "==================================" -ForegroundColor Green
Write-Host ""
Write-Host "Signed MSIX: $SignedMsixPath" -ForegroundColor Cyan
Write-Host "Certificate: $CertPath" -ForegroundColor Cyan
Write-Host ""
Write-Host "INSTALLATION STEPS:" -ForegroundColor Yellow
Write-Host "1. Install the certificate to Trusted Root:" -ForegroundColor Yellow
Write-Host "   - Double-click: $CertPath" -ForegroundColor White
Write-Host "   - Click 'Install Certificate...'" -ForegroundColor White
Write-Host "   - Store Location: Current User" -ForegroundColor White
Write-Host "   - Place in: Trusted Root Certification Authorities" -ForegroundColor White
Write-Host "   - Finish" -ForegroundColor White
Write-Host ""
Write-Host "2. Install the MSIX package:" -ForegroundColor Yellow
Write-Host "   - Double-click: $SignedMsixPath" -ForegroundColor White
Write-Host "   - Click 'Install'" -ForegroundColor White
Write-Host ""
Write-Host "3. Test the installation:" -ForegroundColor Yellow
Write-Host "   - Check Start Menu for 'EF Map Overlay Helper'" -ForegroundColor White
Write-Host "   - Try launching from Start Menu" -ForegroundColor White
Write-Host "   - Test ef-overlay:// protocol from web app" -ForegroundColor White
Write-Host ""
Write-Host "CLEANUP (when done testing):" -ForegroundColor Yellow
Write-Host "- Uninstall app: Settings → Apps → EF Map Overlay Helper → Uninstall" -ForegroundColor White
Write-Host "- Remove cert: certmgr.msc → Trusted Root → Certificates → Delete '$CertName'" -ForegroundColor White
Write-Host ""
Write-Host "⚠️  DO NOT UPLOAD THE SIGNED VERSION TO STORE!" -ForegroundColor Red
Write-Host "    Upload the original unsigned MSIX instead." -ForegroundColor Red
