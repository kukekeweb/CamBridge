[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$Executable,
    [Parameter(Mandatory = $true)]
    [string]$WorkingDirectory,
    [Parameter(Mandatory = $true)]
    [string]$LogDirectory,
    [Parameter(Mandatory = $true)]
    [string]$PidFile
)

$ErrorActionPreference = 'Stop'

if (-not (Test-Path -LiteralPath $Executable -PathType Leaf)) {
    throw "Synthetic publisher executable is missing: $Executable"
}
if (-not (Test-Path -LiteralPath $WorkingDirectory -PathType Container)) {
    throw "Synthetic publisher working directory is missing: $WorkingDirectory"
}

New-Item -ItemType Directory -Path $LogDirectory -Force | Out-Null
Remove-Item -LiteralPath $PidFile -Force -ErrorAction SilentlyContinue

$runId = '{0:yyyyMMdd-HHmmss-fff}-{1}' -f (Get-Date), $PID
$stdoutPath = Join-Path $LogDirectory "synthetic-publisher-$runId.log"
$stderrPath = Join-Path $LogDirectory "synthetic-publisher-$runId-error.log"

$publisher = Start-Process -FilePath $Executable `
    -WorkingDirectory $WorkingDirectory `
    -RedirectStandardOutput $stdoutPath `
    -RedirectStandardError $stderrPath `
    -WindowStyle Hidden `
    -PassThru

$utf8NoBom = [System.Text.UTF8Encoding]::new($false)
[System.IO.File]::WriteAllText($PidFile, [string]$publisher.Id, $utf8NoBom)

Write-Output ("Synthetic publisher started: PID={0}" -f $publisher.Id)
Write-Output ("Synthetic publisher stdout: {0}" -f $stdoutPath)
Write-Output ("Synthetic publisher stderr: {0}" -f $stderrPath)
exit 0
