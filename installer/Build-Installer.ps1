[CmdletBinding()]
param(
    [string]$CertificateThumbprint = '7120C3168A21833EC1B8F4C5139F968C2CFA5B64',
    [switch]$SkipBuild
)

$ErrorActionPreference = 'Stop'
$scriptRoot = Split-Path -Parent $PSCommandPath
$repoRoot   = Resolve-Path (Join-Path $scriptRoot '..')

function Write-Step([string]$msg) { Write-Host "==> $msg" }

# ---------------------------------------------------------------------------
# Locate Inno Setup compiler
# ---------------------------------------------------------------------------
function Find-Iscc {
    $dirs = @(
        Get-ChildItem 'C:\Program Files'        -Directory -Filter 'Inno Setup*' -ErrorAction SilentlyContinue
        Get-ChildItem 'C:\Program Files (x86)'  -Directory -Filter 'Inno Setup*' -ErrorAction SilentlyContinue
    ) | Sort-Object Name -Descending
    foreach ($dir in $dirs) {
        $exe = Join-Path $dir.FullName 'ISCC.exe'
        if (Test-Path $exe) { return $exe }
    }
    return $null
}

function Install-InnoSetup {
    Write-Step 'Inno Setup 6 not found — downloading and installing...'
    $installer = Join-Path $env:TEMP 'innosetup-installer.exe'
    $release = Invoke-RestMethod -Headers @{ 'User-Agent' = 'SteamControllerRemapperBuild' } `
        -Uri 'https://api.github.com/repos/jrsoftware/issrc/releases/latest'
    $asset = $release.assets | Where-Object { $_.name -match '^innosetup-.*\.exe$' } | Select-Object -First 1
    if (-not $asset) { throw 'Could not find Inno Setup installer in the latest GitHub release.' }
    Invoke-WebRequest -Headers @{ 'User-Agent' = 'SteamControllerRemapperBuild' } `
        -Uri $asset.browser_download_url -OutFile $installer
    $proc = Start-Process -FilePath $installer `
        -ArgumentList '/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', '/SP-' `
        -Wait -PassThru
    if ($proc.ExitCode -ne 0) { throw "Inno Setup installer failed with exit code $($proc.ExitCode)." }
    Remove-Item $installer -Force
}

# ---------------------------------------------------------------------------
# Build the C++ desktop app
# ---------------------------------------------------------------------------
function Build-DesktopApp {
    Write-Step 'Building desktop app (CMake Release)...'
    $cmake = 'C:\Program Files\CMake\bin\cmake.exe'
    if (-not (Test-Path $cmake)) { $cmake = 'cmake' }
    $buildDir = Join-Path $repoRoot 'build'
    if (-not (Test-Path $buildDir)) {
        & $cmake -S $repoRoot -B $buildDir -DCMAKE_BUILD_TYPE=Release
    }
    & $cmake --build $buildDir --config Release
    if ($LASTEXITCODE -ne 0) { throw 'CMake build failed.' }
}

# ---------------------------------------------------------------------------
# Build the gamebar widget
# ---------------------------------------------------------------------------
function Build-Widget([string]$Thumbprint) {
    Write-Step 'Building gamebar widget (MSBuild Release x64)...'
    $msbuild = Get-ChildItem 'C:\Program Files (x86)\Microsoft Visual Studio' -Recurse -Filter MSBuild.exe -ErrorAction SilentlyContinue |
        Where-Object { $_.FullName -like '*\amd64\MSBuild.exe' } |
        Sort-Object FullName -Descending | Select-Object -First 1
    if (-not $msbuild) { throw 'MSBuild.exe (amd64) not found. Install VS Build Tools with the UWP workload.' }
    $proj = Join-Path $repoRoot 'gamebar\SteamControllerRemapperWidget\SteamControllerRemapperWidget.csproj'
    # Use the installed SDK version; the project targets 10.0.19041 but any newer SDK works fine
    $sdk = Get-ChildItem 'C:\Program Files (x86)\Windows Kits\10\References' -Directory |
        Sort-Object Name -Descending | Select-Object -First 1
    $sdkVer = if ($sdk) { $sdk.Name } else { '10.0.19041.0' }
    & $msbuild.FullName $proj `
        /p:Configuration=Release /p:Platform=x64 `
        /p:AppxPackageSigningEnabled=true `
        /p:PackageCertificateThumbprint=$Thumbprint `
        /p:AppxBundle=Never `
        /p:TargetPlatformVersion=$sdkVer `
        /t:Restore,Build | Where-Object { $_ -match 'error|warning|Build succeeded|FAILED' }
    if ($LASTEXITCODE -ne 0) { throw 'Gamebar widget build failed.' }
}

# ---------------------------------------------------------------------------
# Locate the built widget package
# ---------------------------------------------------------------------------
function Find-WidgetPackageFolder {
    $appPackages = Join-Path $repoRoot 'gamebar\SteamControllerRemapperWidget\AppPackages'
    if (-not (Test-Path $appPackages)) {
        throw "AppPackages folder not found at $appPackages. Build the gamebar widget in Visual Studio first."
    }
    $candidates = Get-ChildItem -Path $appPackages -Directory | Where-Object {
        (Test-Path (Join-Path $_.FullName '*.msix')) -and
        (Test-Path (Join-Path $_.FullName 'Add-AppDevPackage.ps1'))
    }
    if ($candidates.Count -eq 0) { throw 'No built widget package found under AppPackages.' }
    $preferred = $candidates | Where-Object { $_.Name -like '*_x64_*' }
    if ($preferred) { return $preferred | Sort-Object LastWriteTime -Descending | Select-Object -First 1 }
    return $candidates | Sort-Object LastWriteTime -Descending | Select-Object -First 1
}

# ---------------------------------------------------------------------------
# Certificate helpers
# ---------------------------------------------------------------------------
function Export-PublicCertificate([string]$Thumbprint, [string]$DestinationPath) {
    $cert =
        (Get-ChildItem Cert:\CurrentUser\My   -EA SilentlyContinue | Where-Object { $_.Thumbprint -eq $Thumbprint } | Select-Object -First 1)
    if (-not $cert) {
        $cert = (Get-ChildItem Cert:\LocalMachine\My -EA SilentlyContinue | Where-Object { $_.Thumbprint -eq $Thumbprint } | Select-Object -First 1)
    }
    if ($cert) {
        Export-Certificate -Cert $cert -FilePath $DestinationPath -Force | Out-Null
        return
    }
    # Fall back to any .cer already in BundleArtifacts
    $fallback = Get-ChildItem -Path (Join-Path $repoRoot 'gamebar\SteamControllerRemapperWidget\BundleArtifacts') `
        -Filter *.cer -File -ErrorAction SilentlyContinue |
        Sort-Object LastWriteTime -Descending | Select-Object -First 1
    if ($fallback) {
        Copy-Item -LiteralPath $fallback.FullName -Destination $DestinationPath -Force
        return
    }
    throw "Certificate with thumbprint $Thumbprint was not found in CurrentUser\My or LocalMachine\My, and no fallback .cer exists."
}

function Get-SignToolPath {
    $kitsRoot = 'C:\Program Files (x86)\Windows Kits\10\bin'
    $tool = Get-ChildItem -Path $kitsRoot -Recurse -Filter signtool.exe -ErrorAction SilentlyContinue |
        Where-Object { $_.FullName -like '*\x64\signtool.exe' } |
        Sort-Object FullName -Descending | Select-Object -First 1
    if (-not $tool) { throw 'signtool.exe not found in the Windows SDK.' }
    return $tool.FullName
}

function Ensure-Signed([string]$FilePath, [string]$Thumbprint) {
    $signtool = Get-SignToolPath
    $verify = Start-Process $signtool -ArgumentList "verify /pa `"$FilePath`"" -Wait -PassThru -NoNewWindow
    if ($verify.ExitCode -eq 0) { return }
    Write-Step "Signing $FilePath"
    $sign = Start-Process $signtool -ArgumentList "sign /fd SHA256 /sha1 $Thumbprint /s My `"$FilePath`"" -Wait -PassThru -NoNewWindow
    if ($sign.ExitCode -ne 0) { throw "Failed to sign $FilePath" }
}

# ---------------------------------------------------------------------------
# Download latest usbip-win2 installer
# ---------------------------------------------------------------------------
function Save-UsbIpInstaller([string]$DestDir) {
    New-Item -ItemType Directory -Path $DestDir -Force | Out-Null
    Write-Step 'Downloading latest usbip-win2 x64 installer...'
    $release = Invoke-RestMethod -Headers @{ 'User-Agent' = 'SteamControllerRemapperBuild' } `
        -Uri 'https://api.github.com/repos/vadimgrn/usbip-win2/releases/latest'
    $asset = $release.assets | Where-Object { $_.name -match '^USBip-.*-x64\.exe$' } | Select-Object -First 1
    if (-not $asset) { throw 'No x64 usbip-win2 installer found in the latest release.' }
    # Use a fixed filename so the .iss Source entry is stable
    $dest = Join-Path $DestDir 'USBip-win2-x64.exe'
    Invoke-WebRequest -Headers @{ 'User-Agent' = 'SteamControllerRemapperBuild' } `
        -Uri $asset.browser_download_url -OutFile $dest
    Write-Step "Bundled usbip-win2 $($release.tag_name)"
}

# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------
if (-not $SkipBuild) {
    Build-DesktopApp
    Build-Widget -Thumbprint $CertificateThumbprint
}

$stagingRoot  = Join-Path $scriptRoot 'staging'
$desktopStage = Join-Path $stagingRoot 'Desktop'
$widgetStage  = Join-Path $stagingRoot 'Widget'
$usbipStage   = Join-Path $stagingRoot 'usbip'

if (Test-Path $stagingRoot) { Remove-Item $stagingRoot -Recurse -Force }
New-Item -ItemType Directory -Path $desktopStage | Out-Null
New-Item -ItemType Directory -Path $widgetStage  | Out-Null

# Stage desktop runtime
$releaseDir = Join-Path $repoRoot 'build\Release'
foreach ($name in @('Steam Controller Remapper.exe', 'libVIIPER.dll')) {
    $src = Join-Path $releaseDir $name
    if (-not (Test-Path $src)) { throw "Build output not found: $src" }
    Copy-Item $src -Destination (Join-Path $desktopStage $name)
}
# Desktop exe doesn't need signing for installation; signing is only required for the .msix

# Stage widget package (use a fixed .msix name)
Write-Step 'Locating built widget package...'
$pkgFolder = Find-WidgetPackageFolder
$msix = Get-ChildItem $pkgFolder.FullName -Filter *.msix | Select-Object -First 1
if (-not $msix) { throw "No .msix found in $($pkgFolder.FullName)" }
Ensure-Signed -FilePath $msix.FullName -Thumbprint $CertificateThumbprint
Copy-Item $msix.FullName -Destination (Join-Path $widgetStage 'SteamControllerRemapperWidget.msix')
Export-PublicCertificate -Thumbprint $CertificateThumbprint -DestinationPath (Join-Path $widgetStage 'SteamControllerRemapperWidget.cer')

# Stage usbip installer
Save-UsbIpInstaller -DestDir $usbipStage

# Compile installer
$iscc = Find-Iscc
if (-not $iscc) {
    Install-InnoSetup
    $iscc = Find-Iscc
    if (-not $iscc) { throw 'ISCC.exe still not found after installing Inno Setup.' }
}

$outputDir = Join-Path $scriptRoot 'output'
New-Item -ItemType Directory -Path $outputDir -Force | Out-Null

Write-Step 'Compiling Inno Setup installer...'
& $iscc (Join-Path $scriptRoot 'SteamControllerRemapper.iss')
if ($LASTEXITCODE -ne 0) { throw 'ISCC compilation failed.' }

$setupExe = Join-Path $outputDir 'SteamControllerRemapper-1.7.1-Setup.exe'
Write-Step "Installer ready: $setupExe"
