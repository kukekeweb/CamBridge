[CmdletBinding()]
param(
    [Parameter()]
    [string] $RepositoryRoot
)

$ErrorActionPreference = "Stop"
if ([string]::IsNullOrWhiteSpace($RepositoryRoot)) {
    $RepositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
}
$failures = [System.Collections.Generic.List[string]]::new()

function Assert-File([string] $RelativePath) {
    $path = Join-Path $RepositoryRoot $RelativePath
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        $failures.Add("Missing required file: $RelativePath")
    }
}

function Assert-Contains([string] $RelativePath, [string] $Text, [string] $Description) {
    $path = Join-Path $RepositoryRoot $RelativePath
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { return }
    $content = Get-Content -LiteralPath $path -Raw
    if (-not $content.Contains($Text)) { $failures.Add("$Description ($RelativePath)") }
}

function Assert-NotContains([string] $RelativePath, [string] $Text, [string] $Description) {
    $path = Join-Path $RepositoryRoot $RelativePath
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { return }
    $content = Get-Content -LiteralPath $path -Raw
    if ($content.Contains($Text)) { $failures.Add("$Description ($RelativePath)") }
}

@(
    "web/client/index.html",
    "web/client/styles.css",
    "web/client/package.json",
    "web/client/src/settings.js",
    "web/client/src/capability-probe.js",
    "web/client/src/frame-rate-meter.js",
    "web/client/src/capture-controller.js",
    "web/client/src/diagnostic-matrix.js",
    "web/client/src/constraint-probe.js",
    "web/client/src/stability-test.js",
    "web/client/src/i18n.js",
    "web/client/src/main.js",
    "web/client/README.md",
    "windows/stage1-server/server.mjs",
    "windows/stage1-server/README.md",
    "scripts/generate-local-ca.ps1"
) | ForEach-Object { Assert-File $_ }

Assert-Contains "web/client/index.html" 'lang="ja"' "Japanese document language missing"
@(
    "camera-select", "orientation-select", "resolution-select", "frame-rate-select", "quality-select",
    "start-button", "stop-button", "requested-value", "actual-value", "measured-value",
    "codec-list", "capture-status", "matrix-table", "copy-json-button", "copy-csv-button",
    "constraint-probe-table", "run-constraint-probe-button", "run-all-constraint-probe-button",
    "copy-constraint-json-button", "copy-constraint-csv-button",
    "start-stability-button", "stop-stability-button", "stability-status", "stability-export", "copy-stability-json-button",
    "devices-output", "exposure-output", "capabilities-output", "settings-output", "constraints-output", "errors-output"
) | ForEach-Object { Assert-Contains "web/client/index.html" $_ "Missing required UI element: $_" }

Assert-Contains "web/client/src/i18n.js" "ERROR_DESCRIPTIONS" "Centralized Japanese error descriptions missing"
Assert-Contains "web/client/src/i18n.js" "formatDiagnosticResult" "Centralized diagnostic labels missing"
Assert-Contains "web/client/src/i18n.js" "formatConstraintProbeResult" "Constraint probe labels missing"

Assert-Contains "web/client/src/settings.js" "width: { exact: settings.resolution.width }" "Resolution must use exact constraints"
Assert-Contains "web/client/src/settings.js" "height: { exact: settings.resolution.height }" "Resolution must use exact constraints"
Assert-Contains "web/client/src/settings.js" "frameRate: { exact: settings.frameRate }" "Frame rate must use exact constraints"
Assert-Contains "web/client/index.html" 'value="60"' "60 FPS must be a Stage 1 selectable request"
Assert-Contains "web/client/src/capability-probe.js" "getCapabilities" "Track capabilities probe missing"
Assert-Contains "web/client/src/capability-probe.js" "getSettings" "Track settings probe missing"
Assert-Contains "web/client/src/capability-probe.js" "getConstraints" "Track constraints probe missing"
Assert-Contains "web/client/src/capability-probe.js" "enumerateVideoInputsAfterPermission" "Post-permission device enumeration missing"
Assert-Contains "web/client/src/capability-probe.js" "probeVideoDeviceExposure" "Active/after-stop exposure probe missing"
Assert-Contains "web/client/src/frame-rate-meter.js" "requestVideoFrameCallback" "Empirical frame measurement missing"
Assert-Contains "web/client/src/capability-probe.js" 'getCapabilities("video")' "Runtime codec capability probe missing"
Assert-Contains "web/client/src/diagnostic-matrix.js" "width: 1280, height: 720, frameRate: 30" "Diagnostic 720p30 case missing"
Assert-Contains "web/client/src/diagnostic-matrix.js" "width: 3840, height: 2160, frameRate: 60" "Diagnostic 4K60 case missing"
Assert-Contains "web/client/src/diagnostic-matrix.js" "mismatch-observed" "Diagnostic mismatch observation missing"
Assert-Contains "web/client/src/diagnostic-matrix.js" "serialiseDiagnosticCSV" "Diagnostic CSV export missing"
Assert-Contains "web/client/src/diagnostic-matrix.js" 'return "A"' "Diagnostic A classification missing"
Assert-Contains "web/client/src/diagnostic-matrix.js" 'return "B"' "Diagnostic B classification missing"
Assert-Contains "web/client/src/diagnostic-matrix.js" 'return "C"' "Diagnostic C classification missing"
Assert-Contains "web/client/src/diagnostic-matrix.js" 'request.video.deviceId = { exact: deviceId }' "Diagnostic device ID constraint missing"
Assert-Contains "web/client/src/diagnostic-matrix.js" "deviceIdMatches" "Diagnostic device ID verification missing"
Assert-Contains "web/client/src/diagnostic-matrix.js" "requestedDeviceId" "Requested device ID snapshot missing"
Assert-Contains "web/client/src/constraint-probe.js" "applyConstraints" "Constraint apply probe missing"
Assert-Contains "web/client/src/constraint-probe.js" "min: TARGET_FPS" "Constraint min/ideal probe missing"
Assert-Contains "web/client/src/constraint-probe.js" "createWidthConstraintRequest" "Constraint resolution step missing"
Assert-Contains "web/client/src/stability-test.js" "STABILITY_TEST_DURATION_MS" "600 second stability test duration missing"
Assert-Contains "web/client/src/stability-test.js" "evaluateStabilityReport" "Stability PASS/FAIL evaluation missing"
Assert-Contains "web/client/src/stability-test.js" "unhandledrejection" "Stability unhandled rejection tracking missing"
Assert-Contains "web/client/src/stability-test.js" "visibilitychange" "Stability visibility tracking missing"
Assert-Contains "web/client/src/stability-test.js" "pagehide" "Stability page lifecycle tracking missing"
Assert-Contains "web/client/src/stability-test.js" "settingsChanges" "Stability settings change tracking missing"
Assert-Contains "web/client/src/stability-test.js" "requestedFPSDeficiency" "Stability requested FPS deficiency missing"
Assert-Contains ".github/workflows/web-stage1.yml" "tests/stability-test.test.js" "Stability unit test CI step missing"
Assert-Contains "web/client/package.json" "tests/run-tests.mjs" "Cross-version Node test runner missing"
Assert-Contains "windows/stage1-server/server.mjs" "iPhone access URL" "IP access URL startup diagnostic missing"
Assert-Contains "windows/stage1-server/server.mjs" "Certificate SAN" "Certificate SAN startup diagnostic missing"
Assert-Contains "windows/stage1-server/server.mjs" "Friendly URL" "Friendly URL startup diagnostic missing"

foreach ($path in @(
    "web/client/src/main.js",
    "web/client/src/capture-controller.js",
    "windows/stage1-server/server.mjs"
)) {
    foreach ($forbidden in @(
        "new RTCPeerConnection",
        "new WebSocket",
        "createOffer(",
        "turn:",
        "stun:"
    )) {
        Assert-NotContains $path $forbidden "Stage 2 transport symbol is out of scope: $forbidden"
    }
}

Assert-NotContains "web/client/src/settings.js" "ideal:" "Settings must not encode a weaker ideal fallback"
Assert-NotContains "web/client/src/settings.js" "advanced:" "Settings must not encode an advanced fallback"
Assert-Contains ".gitignore" "windows/stage1-server/certificates/*" "Certificate directory must be ignored"
Assert-Contains ".gitignore" "!windows/stage1-server/certificates/.gitkeep" "Certificate marker exception missing"

if ($failures.Count -gt 0) {
    Write-Error ("Web Stage 1 validation failed:`n - " + ($failures -join "`n - "))
    exit 1
}

Write-Output "Web Stage 1 static validation passed."
