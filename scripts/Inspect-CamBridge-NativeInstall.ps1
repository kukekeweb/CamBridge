[CmdletBinding()]
param(
    [string]$InstallRoot = (Join-Path $env:ProgramFiles 'CamBridge\Native'),
    [string]$MediaSourcePath
)

$ErrorActionPreference = 'Continue'
$manifest = Join-Path $InstallRoot 'cambridge_media_source.active.txt'
if ([string]::IsNullOrWhiteSpace($MediaSourcePath) -and (Test-Path -LiteralPath $manifest -PathType Leaf)) {
    $MediaSourcePath = (Get-Content -LiteralPath $manifest -Raw).Trim()
}
if ([string]::IsNullOrWhiteSpace($MediaSourcePath)) {
    $MediaSourcePath = Join-Path $InstallRoot 'cambridge_media_source.dll'
}
$dll = $MediaSourcePath
Write-Output "Install root: $InstallRoot"
Write-Output "Active Media Source DLL: $dll"
Write-Output "Active manifest: $manifest"
Write-Output "File exists: $(Test-Path -LiteralPath $dll -PathType Leaf)"
if (Test-Path -LiteralPath $dll -PathType Leaf) {
    $item = Get-Item -LiteralPath $dll
    Write-Output ("Length: {0}; LastWriteTime: {1}" -f $item.Length, $item.LastWriteTime)
    Write-Output 'ACL:'
    (Get-Acl -LiteralPath $dll) | Format-List Owner,AccessToString
    Write-Output 'icacls:'
    & icacls.exe $dll
    $zone = Get-Item -LiteralPath $dll -Stream Zone.Identifier -ErrorAction SilentlyContinue
    if ($null -eq $zone) {
        Write-Output 'Zone.Identifier: none'
    } else {
        Write-Output 'Zone.Identifier: present'
        Get-Content -LiteralPath $zone.PSPath
    }
}
$clsid = '{F6DC0D8C-8D0E-4DD2-9F5C-A9B83A2A3A61}'
Write-Output 'HKLM COM registration:'
& reg.exe query "HKLM\Software\Classes\CLSID\$clsid\InprocServer32"
Write-Output 'Service-context note: no impersonated Local Service/SYSTEM file-open was attempted by this non-service diagnostic.'
