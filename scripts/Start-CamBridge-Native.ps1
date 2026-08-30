[CmdletBinding()]
param(
    [string]$BindAddress,
    [ValidateRange(1, 65535)]
    [int]$Port = 8443,
    [string]$ServerScript,
    [string]$WebRoot,
    [string]$ReceiverExecutable,
    [string]$PfxPath,
    [string]$CertificatePath,
    [string]$RootCaPath,
    [string]$NodeExecutable = 'node.exe',
    [string]$LogDirectory,
    [switch]$DryRun
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot

if ([string]::IsNullOrWhiteSpace($ServerScript)) {
    $ServerScript = Join-Path $repoRoot 'windows\stage1-server\server.mjs'
}
if ([string]::IsNullOrWhiteSpace($WebRoot)) {
    $WebRoot = Join-Path $repoRoot 'web\client'
}
if ([string]::IsNullOrWhiteSpace($ReceiverExecutable)) {
    $ReceiverExecutable = Join-Path $repoRoot 'build\native-mvp-libdatachannel\Release\cambridge_native_receiver.exe'
}
if ([string]::IsNullOrWhiteSpace($PfxPath)) {
    $PfxPath = Join-Path $repoRoot 'windows\stage1-server\certificates\cambridge-server-live.pfx'
}
if ([string]::IsNullOrWhiteSpace($CertificatePath)) {
    $CertificatePath = Join-Path $repoRoot 'windows\stage1-server\certificates\cambridge-server.cer'
}
if ([string]::IsNullOrWhiteSpace($RootCaPath)) {
    $RootCaPath = Join-Path $repoRoot 'windows\stage1-server\certificates\cambridge-root-ca.cer'
}
if ([string]::IsNullOrWhiteSpace($LogDirectory)) {
    $LogDirectory = Join-Path $repoRoot 'build\native-mvp\diagnostics\runtime'
}

function Test-PrivateIPv4 {
    param([Parameter(Mandatory = $true)][string]$Address)
    $parts = $Address.Split('.')
    if ($parts.Count -ne 4) { return $false }
    $octets = foreach ($part in $parts) {
        $value = 0
        if (-not [int]::TryParse($part, [ref]$value) -or $value -lt 0 -or $value -gt 255) {
            return $false
        }
        $value
    }
    return $octets[0] -eq 10 -or
        ($octets[0] -eq 172 -and $octets[1] -ge 16 -and $octets[1] -le 31) -or
        ($octets[0] -eq 192 -and $octets[1] -eq 168)
}

function Get-PrivateIPv4 {
    $addresses = @(Get-NetIPAddress -AddressFamily IPv4 -ErrorAction SilentlyContinue |
        Where-Object { -not $_.SkipAsSource -and -not $_.IPAddress.StartsWith('169.254.') } |
        Select-Object -ExpandProperty IPAddress)
    foreach ($address in $addresses) {
        if (Test-PrivateIPv4 $address) { return $address }
    }
    throw 'No private LAN IPv4 address was detected; pass -BindAddress explicitly.'
}

function Assert-File {
    param([Parameter(Mandatory = $true)][string]$Path, [Parameter(Mandatory = $true)][string]$Description)
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "$Description is missing: $Path"
    }
}

function Convert-CertificateToPem {
    param([Parameter(Mandatory = $true)][string]$Certificate)
    if ([IO.Path]::GetExtension($Certificate).ToLowerInvariant() -eq '.pem') {
        return [pscustomobject]@{ Path = $Certificate; Temporary = $false }
    }
    $temporary = Join-Path ([IO.Path]::GetTempPath()) ('cambridge-root-ca-' + [guid]::NewGuid().ToString('N') + '.pem')
    & certutil.exe -encode $Certificate $temporary *> $null
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $temporary -PathType Leaf)) {
        throw "Could not convert public root CA to PEM: $Certificate"
    }
    return [pscustomobject]@{ Path = $temporary; Temporary = $true }
}

function Stop-OwnedProcess {
    param([System.Diagnostics.Process]$Process, [string]$Name)
    if ($null -eq $Process -or $Process.HasExited) { return }
    Write-Output "Stopping $Name (PID $($Process.Id))..."
    Stop-Process -Id $Process.Id -Force -ErrorAction SilentlyContinue
    $Process.WaitForExit(3000)
}

if ([string]::IsNullOrWhiteSpace($BindAddress)) {
    $BindAddress = Get-PrivateIPv4
}
if (-not (Test-PrivateIPv4 $BindAddress)) {
    throw "Refusing non-private bind address: $BindAddress"
}

$httpsUrl = "https://${BindAddress}:${Port}"
$wssUrl = "wss://${BindAddress}:${Port}/signaling"

if ($DryRun) {
    Write-Output 'CamBridge Native runtime dry-run'
    Write-Output "Bind address: $BindAddress"
    Write-Output "HTTPS port: $Port"
    Write-Output "iPhone URL: $httpsUrl"
    Write-Output "WSS URL: $wssUrl"
    Write-Output 'TLS verification: enabled'
    Write-Output 'Virtual Camera: existing CurrentUser registration is not modified'
    Write-Output "Server script: $ServerScript"
    Write-Output "Web root: $WebRoot"
    Write-Output "Receiver: $ReceiverExecutable"
    Write-Output "Logs: $LogDirectory"
    exit 0
}

Assert-File $ServerScript 'HTTPS server script'
if (-not (Test-Path -LiteralPath $WebRoot -PathType Container)) {
    throw "Web Client root is missing: $WebRoot"
}
Assert-File $ReceiverExecutable 'Native receiver executable'
Assert-File $PfxPath 'HTTPS server PFX'
Assert-File $CertificatePath 'HTTPS server certificate'
Assert-File $RootCaPath 'CamBridge root CA certificate'

$node = Get-Command $NodeExecutable -ErrorAction SilentlyContinue
if ($null -eq $node) {
    throw "Node.js executable is not available: $NodeExecutable"
}

New-Item -ItemType Directory -Path $LogDirectory -Force | Out-Null
$serverLog = Join-Path $LogDirectory 'cambridge-server.log'
$receiverLog = Join-Path $LogDirectory 'cambridge-native-receiver.log'
$pem = $null
$server = $null
$receiver = $null
$passwordPointer = [IntPtr]::Zero
$previousPassword = [Environment]::GetEnvironmentVariable('CAMBRIDGE_PFX_PASSWORD', 'Process')
$passwordRestored = $false

try {
    $pem = Convert-CertificateToPem $RootCaPath
    $securePassword = Read-Host 'PFX password (input is not logged)' -AsSecureString
    try {
        $passwordPointer = [Runtime.InteropServices.Marshal]::SecureStringToBSTR($securePassword)
        $plainPassword = [Runtime.InteropServices.Marshal]::PtrToStringBSTR($passwordPointer)
        $env:CAMBRIDGE_PFX_PASSWORD = $plainPassword
        $serverArgs = @(
            $ServerScript,
            '--web-root', $WebRoot,
            '--pfx', $PfxPath,
            '--certificate', $CertificatePath,
            '--bind', $BindAddress,
            '--port', [string]$Port
        )
        $server = Start-Process -FilePath $node.Source `
            -WorkingDirectory $repoRoot `
            -ArgumentList $serverArgs `
            -RedirectStandardOutput $serverLog `
            -RedirectStandardError $serverLog.Replace('.log', '-error.log') `
            -WindowStyle Hidden `
            -PassThru
    } finally {
        if ($passwordPointer -ne [IntPtr]::Zero) {
            [Runtime.InteropServices.Marshal]::ZeroFreeBSTR($passwordPointer)
            $passwordPointer = [IntPtr]::Zero
        }
        $plainPassword = $null
        if ($null -eq $previousPassword) {
            Remove-Item Env:CAMBRIDGE_PFX_PASSWORD -ErrorAction SilentlyContinue
        } else {
            $env:CAMBRIDGE_PFX_PASSWORD = $previousPassword
        }
        $passwordRestored = $true
    }

    $serverHealthy = $false
    for ($attempt = 0; $attempt -lt 40; $attempt++) {
        if ($server.HasExited) {
            throw "HTTPS server exited before /health became ready (exit $($server.ExitCode)); log: $serverLog"
        }
        $healthCode = (& curl.exe -sk -o NUL -w '%{http_code}' "$httpsUrl/health" 2>$null | Out-String).Trim()
        if ($healthCode -eq '200') {
            $serverHealthy = $true
            break
        }
        Start-Sleep -Milliseconds 250
    }
    if (-not $serverHealthy) {
        throw "HTTPS /health did not return 200 within 10 seconds; log: $serverLog"
    }

    $receiverArgs = @(
        '--url', $wssUrl,
        '--ca', $pem.Path,
        '--bind-address', $BindAddress
    )
    $receiver = Start-Process -FilePath $ReceiverExecutable `
        -WorkingDirectory (Split-Path -Parent $ReceiverExecutable) `
        -ArgumentList $receiverArgs `
        -RedirectStandardOutput $receiverLog `
        -RedirectStandardError $receiverLog.Replace('.log', '-error.log') `
        -WindowStyle Hidden `
        -PassThru
    Start-Sleep -Milliseconds 750
    if ($receiver.HasExited) {
        throw "Native receiver exited during startup (exit $($receiver.ExitCode)); log: $receiverLog"
    }

    Write-Output 'CamBridge Native runtime'
    Write-Output "HTTPS: $httpsUrl"
    Write-Output "WSS: $wssUrl"
    Write-Output "Virtual Camera: existing CurrentUser registration (not modified)"
    Write-Output 'TLS verification: enabled; external STUN/TURN: disabled'
    Write-Output 'Status: waiting for iPhone'
    Write-Output "Server log: $serverLog"
    Write-Output "Receiver log: $receiverLog"
    Write-Output 'Press Ctrl+C to exit.'

    while (-not $server.HasExited -and -not $receiver.HasExited) {
        Start-Sleep -Seconds 1
    }
    if ($server.HasExited) {
        throw "HTTPS server stopped unexpectedly (exit $($server.ExitCode)); log: $serverLog"
    }
    throw "Native receiver stopped unexpectedly (exit $($receiver.ExitCode)); log: $receiverLog"
} catch {
    Write-Error $_
    exit 1
} finally {
    if (-not $passwordRestored) {
        if ($null -eq $previousPassword) {
            Remove-Item Env:CAMBRIDGE_PFX_PASSWORD -ErrorAction SilentlyContinue
        } else {
            $env:CAMBRIDGE_PFX_PASSWORD = $previousPassword
        }
    }
    Stop-OwnedProcess $receiver 'native receiver'
    Stop-OwnedProcess $server 'HTTPS server'
    if ($null -ne $pem -and $pem.Temporary -and (Test-Path -LiteralPath $pem.Path)) {
        [IO.File]::Delete($pem.Path)
    }
}
