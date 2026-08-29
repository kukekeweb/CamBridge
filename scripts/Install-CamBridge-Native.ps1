[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$SourceRoot,
    [Parameter(Mandatory = $true)]
    [string]$InstallRoot
)

$ErrorActionPreference = 'Stop'

$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
$principal = [Security.Principal.WindowsPrincipal]::new($identity)
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    Write-Error 'Administrator elevation is required for the Program Files/HKLM installation.'
    exit 740
}

$requiredFiles = @(
    'cambridge_virtual_camera_manager.exe',
    'cambridge_capture_probe.exe',
    'cambridge_synthetic_publisher.exe',
    'cambridge_frame_ipc_probe.exe'
)

Write-Output "Source artifact root: $SourceRoot"
Write-Output "Install root: $InstallRoot"
New-Item -ItemType Directory -Path $InstallRoot -Force | Out-Null

$installRootFull = [System.IO.Path]::GetFullPath($InstallRoot).TrimEnd('\') + '\'
$publisherProcesses = @(Get-CimInstance Win32_Process -Filter "Name='cambridge_synthetic_publisher.exe'" -ErrorAction SilentlyContinue)
foreach ($process in $publisherProcesses) {
    if ([string]::IsNullOrWhiteSpace($process.ExecutablePath)) {
        Write-Output ("Synthetic Publisher PID {0} path is unavailable; it was not stopped." -f $process.ProcessId)
        continue
    }
    $processPath = [System.IO.Path]::GetFullPath($process.ExecutablePath)
    if ($processPath.StartsWith($installRootFull, [System.StringComparison]::OrdinalIgnoreCase)) {
        Stop-Process -Id $process.ProcessId -Force -ErrorAction Stop
        Write-Output ("Stopped existing CamBridge Synthetic Publisher PID {0}: {1}" -f $process.ProcessId, $processPath)
    }
}

foreach ($name in $requiredFiles) {
    $source = Join-Path $SourceRoot $name
    $destination = Join-Path $InstallRoot $name
    if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
        throw "Required build artifact is missing: $source"
    }
    Copy-Item -LiteralPath $source -Destination $destination -Force
    Write-Output "Copied: $destination"
}

$sourceDll = Join-Path $SourceRoot 'cambridge_media_source.dll'
if (-not (Test-Path -LiteralPath $sourceDll -PathType Leaf)) {
    throw "Required build artifact is missing: $sourceDll"
}

$stableDll = Join-Path $InstallRoot 'cambridge_media_source.dll'
$dll = $stableDll
try {
    Copy-Item -LiteralPath $sourceDll -Destination $stableDll -Force
    Write-Output "Copied: $stableDll"
} catch {
    if ($_.Exception -isnot [System.IO.IOException] -and
        $_.Exception -isnot [System.UnauthorizedAccessException]) {
        throw
    }
    $stamp = Get-Date -Format 'yyyyMMdd-HHmmss-fff'
    $versionedName = "cambridge_media_source-$stamp-$PID.dll"
    $dll = Join-Path $InstallRoot $versionedName
    Copy-Item -LiteralPath $sourceDll -Destination $dll -Force
    Write-Output "Stable Media Source DLL is in use; installed side-by-side: $dll"
}

$activeManifest = Join-Path $InstallRoot 'cambridge_media_source.active.txt'
[System.IO.File]::WriteAllText($activeManifest, $dll, [System.Text.UTF8Encoding]::new($false))
Write-Output "Active Media Source DLL: $dll"
Write-Output "Active Media Source manifest: $activeManifest"

Write-Output 'Program Files ACL (inherited/default; no custom ACL was applied):'
$acl = Get-Acl -LiteralPath $dll
$acl | Format-List Owner,AccessToString
Write-Output 'Explicit service-account ACEs:'
foreach ($account in @('NT AUTHORITY\LOCAL SERVICE', 'NT AUTHORITY\SYSTEM')) {
    $entries = @($acl.Access | Where-Object { $_.IdentityReference.Value -eq $account })
    if ($entries.Count -eq 0) {
        Write-Output "${account}: no direct ACE (inherited/group evaluation is required)"
    } else {
        $entries | ForEach-Object {
            Write-Output ("{0}: {1} {2}" -f $account, $_.FileSystemRights, $_.AccessControlType)
        }
    }
}
Write-Output 'icacls:'
& icacls.exe $dll

Write-Output 'Mark of the Web:'
$zone = Get-Item -LiteralPath $dll -Stream Zone.Identifier -ErrorAction SilentlyContinue
if ($null -eq $zone) {
    Write-Output 'Destination Zone.Identifier: none'
} else {
    Write-Output 'Destination Zone.Identifier: present'
    Get-Content -LiteralPath $zone.PSPath
}
foreach ($name in $requiredFiles) {
    $source = Join-Path $SourceRoot $name
    $sourceZone = Get-Item -LiteralPath $source -Stream Zone.Identifier -ErrorAction SilentlyContinue
    $state = if ($null -eq $sourceZone) { 'none' } else { 'present' }
    Write-Output ("Source {0} Zone.Identifier: {1}" -f $name, $state)
}
$sourceZone = Get-Item -LiteralPath $sourceDll -Stream Zone.Identifier -ErrorAction SilentlyContinue
$sourceState = if ($null -eq $sourceZone) { 'none' } else { 'present' }
Write-Output ("Source cambridge_media_source.dll Zone.Identifier: {0}" -f $sourceState)

exit 0
