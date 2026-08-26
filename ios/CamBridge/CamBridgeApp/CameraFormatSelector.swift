import Foundation

enum CameraFormatSelector {
    static func select(request: CaptureRequest, formats: [CameraFormatDescriptor]) -> CapturePlan {
        let output = request.outputResolution.dimensions
        let fpsMatches = formats.filter { $0.minFPS <= request.targetFPS && $0.maxFPS >= request.targetFPS }

        let direct = fpsMatches
            .filter { $0.dimensions == output }
            .sorted { pixelFormatRank($0.pixelFormat) < pixelFormatRank($1.pixelFormat) }
            .first
        if let direct {
            return supportedPlan(request: request, native: direct, path: .directCapture)
        }

        let scaleCandidates = fpsMatches
            .filter { $0.dimensions.pixelCount > output.pixelCount && sameAspectRatio($0.dimensions, output) }
            .sorted {
                if $0.dimensions.pixelCount != $1.dimensions.pixelCount {
                    return $0.dimensions.pixelCount < $1.dimensions.pixelCount
                }
                return pixelFormatRank($0.pixelFormat) < pixelFormatRank($1.pixelFormat)
            }
        if let scaleSource = scaleCandidates.first {
            return supportedPlan(request: request, native: scaleSource, path: .gpuScale)
        }

        return .unsupported(
            for: request,
            warning: "Unsupported: \(request.outputResolution.rawValue) at \(request.targetFPS.clean) FPS is not exposed by the selected lens and no GPU-scale source is available."
        )
    }

    private static func supportedPlan(
        request: CaptureRequest,
        native: CameraFormatDescriptor,
        path: ProcessingPath
    ) -> CapturePlan {
        let warning = native.pixelFormat == .nv12VideoRange ? [] : ["Preferred NV12 video-range output is not the selected native format."]
        return CapturePlan(
            status: .supported,
            requestedResolution: request.outputResolution,
            targetFPS: request.targetFPS,
            nativeDimensions: native.dimensions,
            outputDimensions: request.orientation.outputDimensions(for: request.outputResolution.dimensions),
            processingPath: path,
            nativeFormat: native,
            preferredPixelFormat: .nv12VideoRange,
            outputTransform: FrameTransform(rotationDegrees: request.orientation.rotationDegrees),
            capabilityWarnings: warning
        )
    }

    private static func pixelFormatRank(_ format: PixelFormatKind) -> Int {
        switch format {
        case .nv12VideoRange: return 0
        case .nv12FullRange: return 1
        case .bgra: return 2
        case .other: return 3
        }
    }

    private static func sameAspectRatio(_ lhs: VideoDimensions, _ rhs: VideoDimensions) -> Bool {
        abs(Double(lhs.width) / Double(lhs.height) - Double(rhs.width) / Double(rhs.height)) < 0.01
    }
}
