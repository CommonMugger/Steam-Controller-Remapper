[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$MsixPath,
    [Parameter(Mandatory)][string]$CertPath
)

$ErrorActionPreference = 'Stop'
$log = Join-Path $env:TEMP 'SteamControllerRemapper-Widget-Install.log'

function Log([string]$msg) {
    $line = "$(Get-Date -Format 'yyyy-MM-dd HH:mm:ss.fff') $msg"
    Add-Content -Path $log -Value $line
    Write-Host $line
}

Log "widget-install.ps1 started"
Log "MsixPath=$MsixPath"
Log "CertPath=$CertPath"

# Import the self-signed certificate to LocalMachine\TrustedPeople so Add-AppxPackage
# trusts it regardless of which user context it runs in.
try {
    $cert = New-Object System.Security.Cryptography.X509Certificates.X509Certificate2($CertPath)
    Log "Certificate thumbprint: $($cert.Thumbprint)"

    foreach ($store in @('LocalMachine\TrustedPeople', 'CurrentUser\TrustedPeople')) {
        $existing = Get-ChildItem "Cert:\$store" -ErrorAction SilentlyContinue |
            Where-Object { $_.Thumbprint -eq $cert.Thumbprint } |
            Select-Object -First 1
        if (-not $existing) {
            Import-Certificate -FilePath $CertPath -CertStoreLocation "Cert:\$store" | Out-Null
            Log "Imported certificate to $store"
        } else {
            Log "Certificate already present in $store"
        }
    }
} catch {
    Log "ERROR importing certificate: $_"
    throw
}

# Remove previous version if present
try {
    $old = Get-AppxPackage -Name SteamControllerRemapperWidget -ErrorAction SilentlyContinue
    if ($old) {
        Log "Removing existing package: $($old.PackageFullName)"
        Remove-AppxPackage -Package $old.PackageFullName
        Log "Removed existing package"
    } else {
        Log "No existing package found"
    }
} catch {
    Log "ERROR removing existing package: $_"
    throw
}

# Install the widget
try {
    Log "Running Add-AppxPackage..."
    Add-AppxPackage -Path $MsixPath -ForceApplicationShutdown
    Log "Add-AppxPackage succeeded"
} catch {
    Log "ERROR installing package: $_"
    throw
}

# Restart Game Bar so it picks up the new widget immediately
Log "Restarting Game Bar processes..."
@('GameBar', 'GameBarFTServer', 'XboxGameBarWidgets') | ForEach-Object {
    $proc = Get-Process -Name $_ -ErrorAction SilentlyContinue
    if ($proc) {
        Stop-Process -InputObject $proc -Force
        Log "Stopped $_"
    }
}

Log "widget-install.ps1 completed successfully"
