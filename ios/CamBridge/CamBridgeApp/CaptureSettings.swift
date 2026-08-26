import CoreGraphics
import Foundation

struct VideoDimensions: Equatable, Hashable {
    let width: Int
    let height: Int

    var pixelCount: Int { width * height }
}

enum CameraPosition: String, CaseIterable, Codable {
    case back
    case front
}

enum OutputResolution: String, CaseIterable, Codable, Identifiable {
    case hd1080 = "1920x1080"
    case qhd1440 = "2560x1440"
    case uhd4K = "3840x2160"

    var id: String { rawValue }
    var dimensions: VideoDimensions {
        switch self {
        case .hd1080: return VideoDimensions(width: 1920, height: 1080)
        case .qhd1440: return VideoDimensions(width: 2560, height: 1440)
        case .uhd4K: return VideoDimensions(width: 3840, height: 2160)
        }
    }
}

enum CaptureOrientation: String, CaseIterable, Codable, Identifiable {
    case portrait
    case portraitUpsideDown
    case landscapeLeft
    case landscapeRight

    var id: String { rawValue }
    var rotationDegrees: Int {
        switch self {
        case .portrait: return 90
        case .portraitUpsideDown: return 270
        case .landscapeLeft: return 180
        case .landscapeRight: return 0
        }
    }

    func outputDimensions(for dimensions: VideoDimensions) -> VideoDimensions {
        switch self {
        case .portrait, .portraitUpsideDown:
            return VideoDimensions(width: dimensions.height, height: dimensions.width)
        case .landscapeLeft, .landscapeRight:
            return dimensions
        }
    }
}

enum QualityPreset: String, CaseIterable, Codable, Identifiable {
    case low
    case medium
    case high

    var id: String { rawValue }
}

enum VideoCodec: String, CaseIterable, Codable, Identifiable {
    case h264 = "H.264"
    case hevc = "HEVC"

    var id: String { rawValue }
}

struct CaptureRequest: Equatable {
    let cameraPosition: CameraPosition
    let lensID: String?
    let outputResolution: OutputResolution
    let targetFPS: Double
    let orientation: CaptureOrientation
    let quality: QualityPreset
    let codec: VideoCodec

    init(
        cameraPosition: CameraPosition = .back,
        lensID: String? = nil,
        outputResolution: OutputResolution = .hd1080,
        targetFPS: Double = 60,
        orientation: CaptureOrientation = .landscapeRight,
        quality: QualityPreset = .high,
        codec: VideoCodec = .h264
    ) {
        self.cameraPosition = cameraPosition
        self.lensID = lensID
        self.outputResolution = outputResolution
        self.targetFPS = targetFPS
        self.orientation = orientation
        self.quality = quality
        self.codec = codec
    }
}

enum PixelFormatKind: String, Equatable, Codable {
    case nv12VideoRange = "NV12 video-range"
    case nv12FullRange = "NV12 full-range"
    case bgra = "BGRA"
    case other = "Other"
}

struct FrameTransform: Equatable {
    let rotationDegrees: Int
}

enum ProcessingPath: String, Equatable {
    case directCapture
    case gpuScale
    case unsupported
}

enum CapturePlanStatus: String, Equatable {
    case supported
    case unsupported
}

struct CapturePlan: Equatable {
    let status: CapturePlanStatus
    let requestedResolution: OutputResolution
    let targetFPS: Double
    let nativeDimensions: VideoDimensions?
    let outputDimensions: VideoDimensions
    let processingPath: ProcessingPath
    let nativeFormat: CameraFormatDescriptor?
    let preferredPixelFormat: PixelFormatKind
    let outputTransform: FrameTransform
    let capabilityWarnings: [String]

    static func unsupported(for request: CaptureRequest, warning: String) -> CapturePlan {
        CapturePlan(
            status: .unsupported,
            requestedResolution: request.outputResolution,
            targetFPS: request.targetFPS,
            nativeDimensions: nil,
            outputDimensions: request.orientation.outputDimensions(for: request.outputResolution.dimensions),
            processingPath: .unsupported,
            nativeFormat: nil,
            preferredPixelFormat: .nv12VideoRange,
            outputTransform: FrameTransform(rotationDegrees: request.orientation.rotationDegrees),
            capabilityWarnings: [warning]
        )
    }
}

struct CameraDeviceInfo: Identifiable, Equatable {
    let id: String
    let name: String
    let position: CameraPosition
    let deviceType: String
}

extension Double {
    var clean: String {
        truncatingRemainder(dividingBy: 1) == 0 ? String(Int(self)) : String(format: "%.2f", self)
    }
}
