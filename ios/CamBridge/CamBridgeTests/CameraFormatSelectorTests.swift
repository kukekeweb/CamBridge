import XCTest
@testable import CamBridgeApp

final class CameraFormatSelectorTests: XCTestCase {
    func testSelectsExact1080p60WithoutFallback() {
        let request = CaptureRequest(outputResolution: .hd1080, targetFPS: 60)
        let result = CameraFormatSelector.select(request: request, formats: [
            .init(width: 1920, height: 1080, minFPS: 30, maxFPS: 60,
                  pixelFormat: .nv12VideoRange, isHDRSupported: false, isVideoBinned: false),
            .init(width: 1920, height: 1080, minFPS: 24, maxFPS: 30,
                  pixelFormat: .nv12VideoRange, isHDRSupported: false, isVideoBinned: false)
        ])
        XCTAssertEqual(result.nativeDimensions, .init(width: 1920, height: 1080))
        XCTAssertEqual(result.processingPath, .directCapture)
        XCTAssertEqual(result.targetFPS, 60)
    }

    func testReportsUnsupportedInsteadOfChanging60To30() {
        let request = CaptureRequest(outputResolution: .uhd4K, targetFPS: 60)
        let result = CameraFormatSelector.select(request: request, formats: [
            .init(width: 3840, height: 2160, minFPS: 24, maxFPS: 30,
                  pixelFormat: .nv12VideoRange, isHDRSupported: false, isVideoBinned: false)
        ])
        XCTAssertEqual(result.status, .unsupported)
        XCTAssertEqual(result.targetFPS, 60)
    }

    func testPlansGpuDownscaleWhen1440pNativeFormatIsAbsent() {
        let request = CaptureRequest(outputResolution: .qhd1440, targetFPS: 60)
        let result = CameraFormatSelector.select(request: request, formats: [
            .init(width: 3840, height: 2160, minFPS: 24, maxFPS: 60,
                  pixelFormat: .nv12VideoRange, isHDRSupported: false, isVideoBinned: false)
        ])
        XCTAssertEqual(result.status, .supported)
        XCTAssertEqual(result.processingPath, .gpuScale)
        XCTAssertEqual(result.outputDimensions, .init(width: 2560, height: 1440))
    }

    func testCapturePlanPreservesRequestedFPSAtServiceBoundary() {
        let request = CaptureRequest(outputResolution: .hd1080, targetFPS: 60)
        let plan = CameraFormatSelector.select(request: request, formats: [
            .init(width: 1920, height: 1080, minFPS: 30, maxFPS: 60,
                  pixelFormat: .nv12VideoRange, isHDRSupported: false, isVideoBinned: false)
        ])
        XCTAssertEqual(plan.status, .supported)
        XCTAssertEqual(plan.targetFPS, 60)
        XCTAssertEqual(plan.outputDimensions, .init(width: 1920, height: 1080))
    }

    func testOrientationChangesOutputDimensionsAndCarriesTransform() {
        let request = CaptureRequest(
            outputResolution: .hd1080,
            targetFPS: 60,
            orientation: .portrait
        )
        let plan = CameraFormatSelector.select(request: request, formats: [
            .init(width: 1920, height: 1080, minFPS: 30, maxFPS: 60,
                  pixelFormat: .nv12VideoRange, isHDRSupported: false, isVideoBinned: false)
        ])
        XCTAssertEqual(plan.outputDimensions, .init(width: 1080, height: 1920))
        XCTAssertEqual(plan.outputTransform, .init(rotationDegrees: 90))
    }
}
