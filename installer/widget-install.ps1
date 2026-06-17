[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$MsixPath,
    [Parameter(Mandatory)][string]$CertPath
)

$ErrorActionPreference = 'Stop'

# Import the self-signed certificate so Windows trusts the package
$cert = New-Object System.Security.Cryptography.X509Certificates.X509Certificate2($CertPath)
$existing = Get-ChildItem Cert:\CurrentUser\TrustedPeople -ErrorAction SilentlyContinue |
    Where-Object { $_.Thumbprint -eq $cert.Thumbprint } |
    Select-Object -First 1
if (-not $existing) {
    Import-Certificate -FilePath $CertPath -CertStoreLocation Cert:\CurrentUser\TrustedPeople | Out-Null
}

# Remove previous version if present
$old = Get-AppxPackage -Name SteamControllerRemapperWidget -ErrorAction SilentlyContinue
if ($old) {
    Remove-AppxPackage -Package $old.PackageFullName
}

# Install the widget
Add-AppxPackage -Path $MsixPath

# Restart Game Bar so it picks up the new widget immediately
@('GameBar', 'GameBarFTServer', 'XboxGameBarWidgets') | ForEach-Object {
    Get-Process -Name $_ -ErrorAction SilentlyContinue | Stop-Process -Force
}
