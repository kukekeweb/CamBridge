import AVFoundation
import CoreMedia
import Foundation

struct FrameRateRangeDescriptor: Equatable, Hashable {
    let minFPS: Double
    let maxFPS: Double
}

struct CameraFormatDescriptor: Equatable, Hashable, Identifiable {
    let id: String
    let dimensions: VideoDimensions
    let minFPS: Double
    let maxFPS: Double
    let frameRateRanges: [FrameRateRangeDescriptor]
    let pixelFormat: PixelFormatKind
    let isHDRSupported: Bool
    let isVideoBinned: Bool

    init(
        width: Int,
        height: Int,
        minFPS: Double,
        maxFPS: Double,
        pixelFormat: PixelFormatKind,
        isHDRSupported: Bool,
        isVideoBinned: Bool,
        frameRateRanges: [FrameRateRangeDescriptor]? = nil,
        id: String? = nil
    ) {
        self.dimensions = VideoDimensions(width: width, height: height)
        self.minFPS = minFPS
        self.maxFPS = maxFPS
        self.frameRateRanges = frameRateRanges ?? [FrameRateRangeDescriptor(minFPS: minFPS, maxFPS: maxFPS)]
        self.pixelFormat = pixelFormat
        self.isHDRSupported = isHDRSupported
        self.isVideoBinned = isVideoBinned
        self.id = id ?? "\(width)x\(height)-\(minFPS)-\(maxFPS)-\(pixelFormat.rawValue)-\(isHDRSupported)-\(isVideoBinned)"
    }

    var supports60FPS: Bool { minFPS <= 60 && maxFPS >= 60 }

    static func from(format: AVCaptureDevice.Format, index: Int) -> CameraFormatDescriptor {
        let size = CMVideoFormatDescriptionGetDimensions(format.formatDescription)
        let ranges = format.videoSupportedFrameRateRanges
        let minFPS = ranges.map(\.minFrameRate).min() ?? 0
        let maxFPS = ranges.map(\.maxFrameRate).max() ?? 0
        let frameRateRanges = ranges.map { FrameRateRangeDescriptor(minFPS: $0.minFrameRate, maxFPS: $0.maxFrameRate) }
        let subtype = CMFormatDescriptionGetMediaSubType(format.formatDescription)
        let pixelFormat: PixelFormatKind
        switch subtype {
        case kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange:
            pixelFormat = .nv12VideoRange
        case kCVPixelFormatType_420YpCbCr8BiPlanarFullRange:
            pixelFormat = .nv12FullRange
        case kCVPixelFormatType_32BGRA:
            pixelFormat = .bgra
        default:
            pixelFormat = .other
        }

        return CameraFormatDescriptor(
            width: Int(size.width),
            height: Int(size.height),
            minFPS: minFPS,
            maxFPS: maxFPS,
            pixelFormat: pixelFormat,
            isHDRSupported: format.isVideoHDRSupported,
            isVideoBinned: format.isVideoBinned,
            frameRateRanges: frameRateRanges,
            id: "format-\(index)-\(size.width)x\(size.height)-\(subtype)"
        )
    }

    static func from(format: AVCaptureDevice.Format) -> CameraFormatDescriptor {
        from(format: format, index: 0)
    }

    var summary: String {
        "\(dimensions.width)x\(dimensions.height), \(minFPS.clean)-\(maxFPS.clean) FPS, \(pixelFormat.rawValue), HDR \(isHDRSupported ? "yes" : "no")"
    }
}
