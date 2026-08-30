[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$LogPath,

    [switch]$ResolveOnly
)

$ErrorActionPreference = 'Stop'

function Add-DiagnosticLine {
    param([Parameter(Mandatory = $true)][string]$Text)

    Add-Content -LiteralPath $LogPath -Value $Text
    if (-not [string]::IsNullOrWhiteSpace($env:GITHUB_STEP_SUMMARY)) {
        Add-Content -LiteralPath $env:GITHUB_STEP_SUMMARY -Value $Text
    }
}

try {
    $parent = Split-Path -Parent $LogPath
    if (-not [string]::IsNullOrWhiteSpace($parent)) {
        New-Item -ItemType Directory -Force -Path $parent | Out-Null
    }
    Set-Content -LiteralPath $LogPath -Value 'CamBridge native dependency diagnostics'

    $initialRoot = $env:VCPKG_INSTALLATION_ROOT
    if ([string]::IsNullOrWhiteSpace($initialRoot)) {
        $initialRoot = 'C:\vcpkg'
    }
    Add-DiagnosticLine "initial vcpkg root: $initialRoot"

    $vcpkg = Join-Path $initialRoot 'vcpkg.exe'
    if (-not (Test-Path -LiteralPath $vcpkg -PathType Leaf)) {
        $command = Get-Command vcpkg.exe -ErrorAction SilentlyContinue
        if ($null -ne $command) {
            $vcpkg = $command.Source
        }
    }

    if (-not (Test-Path -LiteralPath $vcpkg -PathType Leaf)) {
        $roots = @()
        $programFiles = [Environment]::GetEnvironmentVariable('ProgramFiles')
        $programFilesX86 = [Environment]::GetEnvironmentVariable('ProgramFiles(x86)')
        if (-not [string]::IsNullOrWhiteSpace($programFiles)) {
            $roots += Join-Path $programFiles 'Microsoft Visual Studio\2022'
        }
        if (-not [string]::IsNullOrWhiteSpace($programFilesX86)) {
            $roots += Join-Path $programFilesX86 'Microsoft Visual Studio\2022'
        }

        foreach ($root in ($roots | Where-Object { Test-Path -LiteralPath $_ -PathType Container })) {
            Add-DiagnosticLine "searching: $root"
            $found = Get-ChildItem -LiteralPath $root -Filter vcpkg.exe -Recurse -File -ErrorAction SilentlyContinue |
                Select-Object -First 1
            if ($null -ne $found) {
                $vcpkg = $found.FullName
                break
            }
        }
    }

    if (-not (Test-Path -LiteralPath $vcpkg -PathType Leaf)) {
        throw "vcpkg executable not found; resolved candidate: $vcpkg"
    }

    Add-DiagnosticLine "vcpkg path: $vcpkg"
    $resolvedRoot = Split-Path -Parent $vcpkg
    Add-DiagnosticLine "resolved vcpkg root: $resolvedRoot"
    if (-not [string]::IsNullOrWhiteSpace($env:GITHUB_ENV)) {
        Add-Content -LiteralPath $env:GITHUB_ENV -Value "CAMBRIDGE_VCPKG_ROOT=$resolvedRoot"
    }

    if ($ResolveOnly) {
        Add-DiagnosticLine 'resolve-only: PASS'
        exit 0
    }

    $versionOutput = & $vcpkg version 2>&1
    $versionExit = $LASTEXITCODE
    $versionOutput | ForEach-Object { Add-DiagnosticLine ([string]$_) }
    Add-DiagnosticLine "vcpkg version exit: $versionExit"
    if ($versionExit -ne 0) {
        throw "vcpkg version failed with exit code $versionExit"
    }

    $installOutput = & $vcpkg install --triplet x64-windows --clean-after-build 2>&1
    $installExit = $LASTEXITCODE
    $installOutput | ForEach-Object { Add-DiagnosticLine ([string]$_) }
    Add-DiagnosticLine "vcpkg install exit: $installExit"
    if ($installExit -ne 0) {
        throw "vcpkg install failed with exit code $installExit"
    }
}
catch {
    $errorText = $_ | Out-String
    Add-DiagnosticLine 'vcpkg diagnostic failure:'
    Add-DiagnosticLine $errorText
    exit 1
}
