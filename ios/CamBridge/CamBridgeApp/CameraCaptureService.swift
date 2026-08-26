import AVFoundation
import Combine
import CoreMedia
import CoreVideo
import Foundation
import QuartzCore

enum CaptureServiceError: LocalizedError {
    case cameraUnavailable
    case permissionDenied
    case unsupported(String)
    case configurationFailed(String)

    var errorDescription: String? {
        switch self {
        case .cameraUnavailable: return "Camera unavailable"
        case .permissionDenied: return "Camera permission denied"
        case .unsupported(let message): return message
        case .configurationFailed(let message): return message
        }
    }
}

final class CameraCaptureService: NSObject, ObservableObject, AVCaptureVideoDataOutputSampleBufferDelegate {
    @Published private(set) var availableCameras: [CameraDeviceInfo] = []
    @Published private(set) var availableLenses: [CameraDeviceInfo] = []
    @Published private(set) var availableFormats: [CameraFormatDescriptor] = []
    @Published private(set) var activeFormatDescription: CameraFormatDescriptor?
    @Published private(set) var latestPlan: CapturePlan?
    @Published private(set) var statisticsSnapshot = CaptureStatisticsSnapshot(
        deliveredFrames: 0,
        droppedFrames: 0,
        fps: 0,
        dropReasons: [:]
    )
    @Published private(set) var actualPixelFormat: PixelFormatKind?
    @Published private(set) var activeFrameRateReadback: Double?
    @Published private(set) var status = "Not started"
    @Published private(set) var isRunning = false

    let session: AVCaptureSession
    let previewLayer: AVCaptureVideoPreviewLayer

    private let sessionQueue = DispatchQueue(label: "com.cambridge.capture")
    private let videoOutput = AVCaptureVideoDataOutput()
    private var selectedDevice: AVCaptureDevice?
    private var statistics = CaptureStatistics()

    override init() {
        session = AVCaptureSession()
        previewLayer = AVCaptureVideoPreviewLayer(session: session)
        previewLayer.videoGravity = .resizeAspect
        super.init()
    }

    func requestCameraAccess() async -> Bool {
        await AVCaptureDevice.requestAccess(for: .video)
    }

    func refreshDevices(position: CameraPosition) {
        let devices = Self.discoverDevices(position: position)
        availableLenses = devices.map(Self.deviceInfo)
        availableCameras = Self.discoverAllDevices().map(Self.deviceInfo)
        if let first = devices.first {
            selectedDevice = first
            availableFormats = Self.formats(for: first)
        } else {
            selectedDevice = nil
            availableFormats = []
            status = "No \(position.rawValue) camera found"
        }
    }

    func start(request: CaptureRequest) throws {
        guard AVCaptureDevice.authorizationStatus(for: .video) == .authorized else {
            status = "Camera permission required"
            throw CaptureServiceError.permissionDenied
        }

        let devices = Self.discoverDevices(position: request.cameraPosition)
        guard let device = devices.first(where: { $0.uniqueID == request.lensID }) ?? devices.first else {
            status = "Camera unavailable"
            throw CaptureServiceError.cameraUnavailable
        }

        let descriptors = Self.formats(for: device)
        let plan = CameraFormatSelector.select(request: request, formats: descriptors)
        latestPlan = plan
        availableFormats = descriptors
        availableLenses = devices.map(Self.deviceInfo)
        selectedDevice = device

        guard plan.status == .supported, let nativeDescriptor = plan.nativeFormat else {
            status = plan.capabilityWarnings.first ?? "Unsupported"
            throw CaptureServiceError.unsupported(status)
        }

        guard let nativeIndex = descriptors.firstIndex(of: nativeDescriptor), nativeIndex < device.formats.count else {
            status = "Selected native format could not be resolved"
            throw CaptureServiceError.configurationFailed(status)
        }

        statistics = CaptureStatistics()
        statisticsSnapshot = statistics.snapshot(now: 0)
        status = "Configuring \(nativeDescriptor.summary)"

        var configurationError: Error?
        sessionQueue.sync {
            session.beginConfiguration()
            session.sessionPreset = .inputPriority
            defer { session.commitConfiguration() }

            session.inputs.forEach { session.removeInput($0) }
            session.outputs.forEach { session.removeOutput($0) }

            do {
                let input = try AVCaptureDeviceInput(device: device)
                guard session.canAddInput(input) else {
                    throw CaptureServiceError.configurationFailed("Cannot add camera input")
                }
                session.addInput(input)

                guard session.canAddOutput(videoOutput) else {
                    throw CaptureServiceError.configurationFailed("Cannot add video output")
                }
                videoOutput.setSampleBufferDelegate(self, queue: sessionQueue)
                videoOutput.alwaysDiscardsLateVideoFrames = true
                if videoOutput.availableVideoCVPixelFormatTypes.contains(where: {
                    $0.uint32Value == kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange
                }) {
                    videoOutput.videoSettings = [
                        kCVPixelBufferPixelFormatTypeKey as String: Int(kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange)
                    ]
                }
                session.addOutput(videoOutput)

                try device.lockForConfiguration()
                device.activeFormat = device.formats[nativeIndex]
                let timescale = Int32(max(1, Int(request.targetFPS.rounded())))
                let duration = CMTime(value: 1, timescale: timescale)
                device.activeVideoMinFrameDuration = duration
                device.activeVideoMaxFrameDuration = duration
                device.unlockForConfiguration()
            } catch {
                configurationError = error
            }
        }

        if let configurationError {
            status = configurationError.localizedDescription
            throw CaptureServiceError.configurationFailed(status)
        }

        let activeIndex = device.formats.firstIndex { $0 === device.activeFormat } ?? nativeIndex
        let activeDescriptor = CameraFormatDescriptor.from(format: device.activeFormat, index: activeIndex)
        let activeMinDuration = device.activeVideoMinFrameDuration
        let activeMaxDuration = device.activeVideoMaxFrameDuration
        let activeFPS = Self.fps(from: activeMinDuration)
        activeFrameRateReadback = activeFPS
        guard activeFPS.isFinite, abs(activeFPS - request.targetFPS) < 0.5,
              abs(Self.fps(from: activeMaxDuration) - request.targetFPS) < 0.5 else {
            status = "Unsupported: active frame duration is not \(request.targetFPS.clean) FPS"
            throw CaptureServiceError.unsupported(status)
        }
        activeFormatDescription = activeDescriptor
        latestPlan = CapturePlan(
            status: .supported,
            requestedResolution: plan.requestedResolution,
            targetFPS: plan.targetFPS,
            nativeDimensions: activeDescriptor.dimensions,
            outputDimensions: plan.outputDimensions,
            processingPath: plan.processingPath,
            nativeFormat: activeDescriptor,
            preferredPixelFormat: plan.preferredPixelFormat,
            outputTransform: plan.outputTransform,
            capabilityWarnings: plan.capabilityWarnings
        )

        sessionQueue.async { [weak self] in
            guard let self else { return }
            self.session.startRunning()
            DispatchQueue.main.async {
                self.isRunning = true
                self.status = "Running"
            }
        }
    }

    func stop() {
        sessionQueue.async { [weak self] in
            guard let self else { return }
            self.session.stopRunning()
            DispatchQueue.main.async {
                self.isRunning = false
                self.status = "Stopped"
            }
        }
    }

    func captureOutput(_ output: AVCaptureOutput, didOutput sampleBuffer: CMSampleBuffer, from connection: AVCaptureConnection) {
        guard let pixelBuffer = CMSampleBufferGetImageBuffer(sampleBuffer) else { return }
        statistics.recordFrame(presentationTimestamp: CMSampleBufferGetPresentationTimeStamp(sampleBuffer))
        let pixelFormat = Self.pixelFormatKind(for: CVPixelBufferGetPixelFormatType(pixelBuffer))
        let snapshot = statistics.snapshot(now: CACurrentMediaTime())
        DispatchQueue.main.async { [weak self] in
            self?.statisticsSnapshot = snapshot
            self?.actualPixelFormat = pixelFormat
        }
    }

    func captureOutput(_ output: AVCaptureOutput, didDrop sampleBuffer: CMSampleBuffer, from connection: AVCaptureConnection) {
        let reason = Self.dropReason(from: sampleBuffer)
        statistics.recordDrop(reason: reason)
        let snapshot = statistics.snapshot(now: CACurrentMediaTime())
        DispatchQueue.main.async { [weak self] in
            self?.statisticsSnapshot = snapshot
        }
    }

    private static func discoverAllDevices() -> [AVCaptureDevice] {
        let types: [AVCaptureDevice.DeviceType] = [
            .builtInWideAngleCamera,
            .builtInUltraWideCamera,
            .builtInTelephotoCamera,
            .builtInDualCamera,
            .builtInTripleCamera,
            .builtInTrueDepthCamera
        ]
        return AVCaptureDevice.DiscoverySession(deviceTypes: types, mediaType: .video, position: .unspecified).devices
    }

    private static func discoverDevices(position: CameraPosition) -> [AVCaptureDevice] {
        let avPosition: AVCaptureDevice.Position = position == .front ? .front : .back
        return discoverAllDevices().filter { $0.position == avPosition }
    }

    private static func deviceInfo(_ device: AVCaptureDevice) -> CameraDeviceInfo {
        CameraDeviceInfo(
            id: device.uniqueID,
            name: device.localizedName,
            position: device.position == .front ? .front : .back,
            deviceType: device.deviceType.rawValue
        )
    }

    private static func formats(for device: AVCaptureDevice) -> [CameraFormatDescriptor] {
        device.formats.enumerated().map { CameraFormatDescriptor.from(format: $0.element, index: $0.offset) }
    }

    private static func pixelFormatKind(for subtype: OSType) -> PixelFormatKind {
        switch subtype {
        case kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange: return .nv12VideoRange
        case kCVPixelFormatType_420YpCbCr8BiPlanarFullRange: return .nv12FullRange
        case kCVPixelFormatType_32BGRA: return .bgra
        default: return .other
        }
    }

    private static func fps(from duration: CMTime) -> Double {
        let seconds = CMTimeGetSeconds(duration)
        return seconds > 0 && seconds.isFinite ? 1 / seconds : .nan
    }

    private static func dropReason(from sampleBuffer: CMSampleBuffer) -> String {
        guard let attachments = CMSampleBufferGetSampleAttachmentsArray(sampleBuffer, createIfNecessary: false) as? [[String: Any]],
              let value = attachments.first?[kCMSampleBufferAttachmentKey_DroppedFrameReason as String] else {
            return "unknown"
        }
        return String(describing: value)
    }
}
