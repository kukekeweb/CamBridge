[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path

$requiredFiles = @(
    'ios/CamBridge/CamBridge.xcodeproj/project.pbxproj',
    'ios/CamBridge/CamBridgeApp/CamBridgeApp.swift',
    'ios/CamBridge/CamBridgeApp/ContentView.swift',
    'ios/CamBridge/CamBridgeApp/CaptureSettings.swift',
    'ios/CamBridge/CamBridgeApp/CameraFormatDescriptor.swift',
    'ios/CamBridge/CamBridgeApp/CameraFormatSelector.swift',
    'ios/CamBridge/CamBridgeApp/CaptureStatistics.swift',
    'ios/CamBridge/CamBridgeApp/CameraCaptureService.swift',
    'ios/CamBridge/CamBridgeApp/CameraPreviewView.swift',
    'ios/CamBridge/CamBridgeApp/Info.plist',
    'ios/CamBridge/CamBridgeTests/CameraFormatSelectorTests.swift',
    'ios/CamBridge/CamBridgeTests/CaptureStatisticsTests.swift',
    'ios/README.md',
    'docs/superpowers/specs/2026-08-27-cambridge-stage0-stage1-design.md',
    '.github/workflows/ios-device-ipa.yml'
)

$requiredLabels = @(
    'Camera', 'Lens', 'Resolution', 'Target FPS', 'Actual FPS',
    'Quality', 'Encoder', 'Orientation', 'Status', 'Dropped Frames'
)

$missing = [System.Collections.Generic.List[string]]::new()
foreach ($relativePath in $requiredFiles) {
    $fullPath = Join-Path $repoRoot $relativePath
    if (-not (Test-Path -LiteralPath $fullPath -PathType Leaf)) {
        $missing.Add("Missing file: $relativePath")
    }
}

$contentFiles = @(
    'ios/CamBridge/CamBridgeApp/ContentView.swift',
    'ios/CamBridge/CamBridgeApp/CaptureSettings.swift',
    'ios/CamBridge/CamBridgeApp/CameraCaptureService.swift',
    'ios/CamBridge/CamBridgeApp/CameraFormatDescriptor.swift',
    'ios/CamBridge/CamBridgeApp/CameraFormatSelector.swift',
    'ios/CamBridge/CamBridgeApp/Info.plist',
    'ios/CamBridge/CamBridge.xcodeproj/project.pbxproj',
    '.github/workflows/ios-device-ipa.yml'
)
$content = ($contentFiles | ForEach-Object {
    $path = Join-Path $repoRoot $_
    if (Test-Path -LiteralPath $path) { Get-Content -Raw -LiteralPath $path }
}) -join "`n"

foreach ($label in $requiredLabels) {
    if ($content -notlike "*$label*") {
        $missing.Add("Missing UI label: $label")
    }
}

$requiredSymbols = @(
    'videoSupportedFrameRateRanges',
    'activeFormat',
    'activeVideoMinFrameDuration',
    'activeVideoMaxFrameDuration',
    'captureOutput(_ output: AVCaptureOutput, didDrop',
    'kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange',
    'targetFPS: Double = 60',
    'status == .supported',
    'runs-on: xcode-27',
    'generic/platform=iOS',
    'CODE_SIGNING_ALLOWED=NO',
    'CODE_SIGNING_REQUIRED=NO',
    'actions/upload-artifact@v4'
)
foreach ($symbol in $requiredSymbols) {
    if ($content -notlike "*$symbol*") {
        $missing.Add("Missing required symbol/text: $symbol")
    }
}

if ($content -match 'targetFPS\s*=\s*30|targetFPS\s*:\s*30') {
    $missing.Add('Automatic 30 FPS fallback text detected')
}
if ($content -notmatch 'NSCameraUsageDescription') {
    $missing.Add('Missing NSCameraUsageDescription')
}
if ($content -match 'NSMicrophoneUsageDescription') {
    $missing.Add('Unexpected microphone permission')
}

if ($missing.Count -gt 0) {
    $missing | ForEach-Object { Write-Error $_ }
    exit 1
}

Write-Output "Stage 1 static validation passed: $($requiredFiles.Count) files and $($requiredLabels.Count) UI labels checked."
