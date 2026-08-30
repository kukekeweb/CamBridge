[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$LogPath,

    [switch]$ResolveOnly
)

$ErrorActionPreference = 'Stop'

function Add-DiagnosticLine {
    param([Parameter(Mandatory = $true)][AllowEmptyString()][string]$Text)

    Add-Content -LiteralPath $LogPath -Value $Text
    if (-not [string]::IsNullOrWhiteSpace($env:GITHUB_STEP_SUMMARY)) {
        Add-Content -LiteralPath $env:GITHUB_STEP_SUMMARY -Value $Text
    }
}

function Bootstrap-Vcpkg {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Baseline
    )

    $runnerTemp = $env:RUNNER_TEMP
    if ([string]::IsNullOrWhiteSpace($runnerTemp)) {
        $runnerTemp = [System.IO.Path]::GetTempPath()
    }
    $bootstrapRoot = Join-Path $runnerTemp 'CamBridge-vcpkg'
    $vcpkg = Join-Path $bootstrapRoot 'vcpkg.exe'
    if (Test-Path -LiteralPath $vcpkg -PathType Leaf) {
        Add-DiagnosticLine "using bootstrapped vcpkg: $vcpkg"
        return $vcpkg
    }

    $git = Get-Command git.exe -ErrorAction SilentlyContinue
    if ($null -eq $git) {
        throw 'vcpkg executable not found and git.exe is unavailable for bootstrap'
    }

    if (-not (Test-Path -LiteralPath (Join-Path $bootstrapRoot '.git') -PathType Container)) {
        if (Test-Path -LiteralPath $bootstrapRoot -PathType Container) {
            Remove-Item -LiteralPath $bootstrapRoot -Recurse -Force
        }
        Add-DiagnosticLine "bootstrapping vcpkg repository into: $bootstrapRoot"
        $cloneOutput = & $git.Source clone --filter=blob:none https://github.com/microsoft/vcpkg.git $bootstrapRoot 2>&1
        $cloneExit = $LASTEXITCODE
        $cloneOutput | ForEach-Object { Add-DiagnosticLine ([string]$_) }
        if ($cloneExit -ne 0) {
            throw "vcpkg repository clone failed with exit code $cloneExit"
        }
    }

    Add-DiagnosticLine "checking out vcpkg baseline: $Baseline"
    $fetchOutput = & $git.Source -C $bootstrapRoot fetch --depth 1 origin $Baseline 2>&1
    $fetchExit = $LASTEXITCODE
    $fetchOutput | ForEach-Object { Add-DiagnosticLine ([string]$_) }
    if ($fetchExit -ne 0) {
        throw "vcpkg baseline fetch failed with exit code $fetchExit"
    }
    $checkoutOutput = & $git.Source -C $bootstrapRoot checkout --detach $Baseline 2>&1
    $checkoutExit = $LASTEXITCODE
    $checkoutOutput | ForEach-Object { Add-DiagnosticLine ([string]$_) }
    if ($checkoutExit -ne 0) {
        throw "vcpkg baseline checkout failed with exit code $checkoutExit"
    }

    $bootstrapScript = Join-Path $bootstrapRoot 'bootstrap-vcpkg.bat'
    if (-not (Test-Path -LiteralPath $bootstrapScript -PathType Leaf)) {
        throw "vcpkg bootstrap script is missing: $bootstrapScript"
    }
    Add-DiagnosticLine 'running vcpkg bootstrap with telemetry disabled'
    $bootstrapOutput = & cmd.exe /d /c "`"$bootstrapScript`" -disableMetrics" 2>&1
    $bootstrapExit = $LASTEXITCODE
    $bootstrapOutput | ForEach-Object { Add-DiagnosticLine ([string]$_) }
    if ($bootstrapExit -ne 0) {
        throw "vcpkg bootstrap failed with exit code $bootstrapExit"
    }
    if (-not (Test-Path -LiteralPath $vcpkg -PathType Leaf)) {
        throw "vcpkg bootstrap completed without producing: $vcpkg"
    }
    return $vcpkg
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
        $manifestPath = Join-Path (Get-Location) 'vcpkg.json'
        if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) {
            throw "vcpkg executable not found and manifest is missing: $manifestPath"
        }
        $manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
        $baseline = [string]$manifest.'builtin-baseline'
        if ([string]::IsNullOrWhiteSpace($baseline)) {
            throw 'vcpkg executable not found and vcpkg.json has no builtin-baseline'
        }
        $vcpkg = Bootstrap-Vcpkg -Baseline $baseline
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
