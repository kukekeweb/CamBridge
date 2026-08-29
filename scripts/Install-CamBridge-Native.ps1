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
    'cambridge_media_source.dll',
    'cambridge_virtual_camera_manager.exe',
    'cambridge_capture_probe.exe',
    'cambridge_synthetic_publisher.exe'
)

Write-Output "Source artifact root: $SourceRoot"
Write-Output "Install root: $InstallRoot"
New-Item -ItemType Directory -Path $InstallRoot -Force | Out-Null

foreach ($name in $requiredFiles) {
    $source = Join-Path $SourceRoot $name
    $destination = Join-Path $InstallRoot $name
    if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
        throw "Required build artifact is missing: $source"
    }
    Copy-Item -LiteralPath $source -Destination $destination -Force
    Write-Output "Copied: $destination"
}

Write-Output 'Program Files ACL (inherited/default; no custom ACL was applied):'
$dll = Join-Path $InstallRoot 'cambridge_media_source.dll'
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

exit 0
