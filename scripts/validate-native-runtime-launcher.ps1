[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$launcher = Join-Path $PSScriptRoot 'Start-CamBridge-Native.ps1'

if (-not (Test-Path -LiteralPath $launcher -PathType Leaf)) {
    throw "Native runtime launcher is missing: $launcher"
}

$output = & pwsh.exe -NoProfile -ExecutionPolicy Bypass -File $launcher `
    -DryRun `
    -BindAddress '192.168.11.2' `
    -Port 8443 `
    -ServerScript (Join-Path $repoRoot 'windows\stage1-server\server.mjs') `
    -WebRoot (Join-Path $repoRoot 'web\client') `
    -ReceiverExecutable (Join-Path $repoRoot 'build\native-mvp-libdatachannel\Release\cambridge_native_receiver.exe') `
    -PfxPath (Join-Path $repoRoot 'windows\stage1-server\certificates\cambridge-server-live.pfx') `
    -CertificatePath (Join-Path $repoRoot 'windows\stage1-server\certificates\cambridge-server.cer') `
    -RootCaPath (Join-Path $repoRoot 'windows\stage1-server\certificates\cambridge-root-ca.cer') 2>&1

if ($LASTEXITCODE -ne 0) {
    throw "Native runtime launcher dry-run failed with exit code $LASTEXITCODE`n$($output -join [Environment]::NewLine)"
}

$text = $output -join [Environment]::NewLine
$required = @(
    'CamBridge Native runtime dry-run',
    'Bind address: 192.168.11.2',
    'HTTPS port: 8443',
    'iPhone URL: https://192.168.11.2:8443',
    'WSS URL: wss://192.168.11.2:8443/signaling',
    'TLS verification: enabled',
    'Virtual Camera: existing CurrentUser registration is not modified'
)
foreach ($marker in $required) {
    if ($text -notlike "*$marker*") {
        throw "Dry-run output is missing: $marker`n$text"
    }
}
if ($text -like '*allow-insecure-tls*') {
    throw 'Normal runtime launcher must not enable insecure TLS.'
}

Write-Output 'Native runtime launcher dry-run validation passed.'
