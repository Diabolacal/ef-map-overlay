# Create Self-Signed Test Certificate for MSIX Development

param(
    [string]$CertPassword = "test123"
)

$ErrorActionPreference = 'Stop'

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$certPath = Join-Path $scriptDir 'ef-overlay-test-cert.pfx'

# Certificate subject (must match manifest Publisher)
$subject = "CN=EF Map Project Test Certificate"

Write-Host "Creating self-signed test certificate..." -ForegroundColor Cyan
Write-Host "Subject: $subject"
Write-Host "Valid: 2 years"

# Create certificate
$cert = New-SelfSignedCertificate -Type CodeSigningCert -Subject $subject -KeyUsage DigitalSignature -FriendlyName "EF Map Overlay Test Certificate" -CertStoreLocation "Cert:\CurrentUser\My" -TextExtension @("2.5.29.37={text}1.3.6.1.5.5.7.3.3", "2.5.29.19={text}") -NotAfter (Get-Date).AddYears(2)

Write-Host "Certificate created" -ForegroundColor Green

# Export to PFX
$securePassword = ConvertTo-SecureString -String $CertPassword -Force -AsPlainText
Export-PfxCertificate -Cert $cert -FilePath $certPath -Password $securePassword | Out-Null

Write-Host "Exported to: $certPath" -ForegroundColor Green

# Install to Trusted Root
Write-Host "Installing certificate to Trusted Root..." -ForegroundColor Cyan

try {
    $rootStore = New-Object System.Security.Cryptography.X509Certificates.X509Store([System.Security.Cryptography.X509Certificates.StoreName]::Root, [System.Security.Cryptography.X509Certificates.StoreLocation]::LocalMachine)
    $rootStore.Open([System.Security.Cryptography.X509Certificates.OpenFlags]::ReadWrite)
    $rootStore.Add($cert)
    $rootStore.Close()
    Write-Host "Certificate installed to Trusted Root - LocalMachine" -ForegroundColor Green
} catch {
    Write-Warning "Failed to install to LocalMachine root. Trying CurrentUser root..."
    $rootStore = New-Object System.Security.Cryptography.X509Certificates.X509Store([System.Security.Cryptography.X509Certificates.StoreName]::Root, [System.Security.Cryptography.X509Certificates.StoreLocation]::CurrentUser)
    $rootStore.Open([System.Security.Cryptography.X509Certificates.OpenFlags]::ReadWrite)
    $rootStore.Add($cert)
    $rootStore.Close()
    Write-Host "Certificate installed to Trusted Root - CurrentUser" -ForegroundColor Green
}

# Update manifest
Write-Host "Updating Package.appxmanifest..." -ForegroundColor Cyan

$manifestPath = Join-Path $scriptDir 'Package.appxmanifest'
$manifestContent = Get-Content $manifestPath -Raw
$updatedContent = $manifestContent -replace 'Publisher="CN=PLACEHOLDER_PUBLISHER"', "Publisher=`"$subject`""
Set-Content $manifestPath $updatedContent -NoNewline

Write-Host "Manifest updated: Publisher=$subject" -ForegroundColor Green

# Show certificate details
$thumbprint = $cert.Thumbprint

Write-Host "`nSetup complete!" -ForegroundColor Green
Write-Host "Certificate Thumbprint: $thumbprint"
Write-Host "Subject: $subject"
Write-Host "PFX Path: $certPath"
Write-Host "`nNext steps:"
Write-Host "  1. Rebuild MSIX: .\build_msix.ps1 -Config Debug"
Write-Host "  2. Sign MSIX: .\sign_msix.ps1 -Config Debug"
Write-Host "  3. Install signed MSIX"
Write-Host "`nNote: Remove certificate from Trusted Root after testing using certmgr.msc"
