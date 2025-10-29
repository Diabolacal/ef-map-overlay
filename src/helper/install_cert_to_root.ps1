# Install certificate to Trusted Root (simplified for external PowerShell)
$ErrorActionPreference = 'Stop'

$thumbprint = "7A64ECA5AA71242EC797BC1A86E97C79261A36BB"

Write-Host "Installing certificate $thumbprint to Trusted Root..." -ForegroundColor Cyan

# Get certificate from CurrentUser\My
$cert = Get-ChildItem Cert:\CurrentUser\My | Where-Object { $_.Thumbprint -eq $thumbprint }

if (!$cert) {
    Write-Error "Certificate not found in CurrentUser\My store"
}

Write-Host "Found certificate: $($cert.Subject)"

# Try LocalMachine first (requires admin)
try {
    $rootStore = New-Object System.Security.Cryptography.X509Certificates.X509Store([System.Security.Cryptography.X509Certificates.StoreName]::Root, [System.Security.Cryptography.X509Certificates.StoreLocation]::LocalMachine)
    $rootStore.Open([System.Security.Cryptography.X509Certificates.OpenFlags]::ReadWrite)
    $rootStore.Add($cert)
    $rootStore.Close()
    Write-Host "SUCCESS: Certificate installed to Trusted Root (LocalMachine)" -ForegroundColor Green
} catch {
    Write-Warning "Failed to install to LocalMachine (no admin rights). Trying CurrentUser..."
    $rootStore = New-Object System.Security.Cryptography.X509Certificates.X509Store([System.Security.Cryptography.X509Certificates.StoreName]::Root, [System.Security.Cryptography.X509Certificates.StoreLocation]::CurrentUser)
    $rootStore.Open([System.Security.Cryptography.X509Certificates.OpenFlags]::ReadWrite)
    $rootStore.Add($cert)
    $rootStore.Close()
    Write-Host "SUCCESS: Certificate installed to Trusted Root (CurrentUser)" -ForegroundColor Green
}

Write-Host "`nDone! Press any key to exit..."
$null = $Host.UI.RawUI.ReadKey("NoEcho,IncludeKeyDown")
