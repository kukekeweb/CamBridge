import SwiftUI
import Foundation

struct ContentView: View {
    @StateObject private var captureService = CameraCaptureService()
    @State private var cameraPosition: CameraPosition = .back
    @State private var lensID = ""
    @State private var resolution: OutputResolution = .hd1080
    @State private var targetFPS = 60.0
    @State private var quality: QualityPreset = .high
    @State private var codec: VideoCodec = .h264
    @State private var orientation: CaptureOrientation = .landscapeRight

    var body: some View {
        NavigationStack {
            ScrollView {
                VStack(alignment: .leading, spacing: 16) {
                    CameraPreviewView(previewLayer: captureService.previewLayer)
                        .frame(minHeight: 220)
                        .clipShape(RoundedRectangle(cornerRadius: 12))

                    GroupBox("Capture settings") {
                        VStack(alignment: .leading, spacing: 10) {
                            Picker("Camera", selection: $cameraPosition) {
                                Text("Back").tag(CameraPosition.back)
                                Text("Front").tag(CameraPosition.front)
                            }
                            .onChange(of: cameraPosition) { _, newValue in
                                if captureService.isRunning {
                                    captureService.stop()
                                }
                                lensID = ""
                                captureService.refreshDevices(position: newValue)
                            }

                            Picker("Lens", selection: $lensID) {
                                Text("Automatic").tag("")
                                ForEach(captureService.availableLenses) { lens in
                                    Text(lens.name).tag(lens.id)
                                }
                            }

                            Picker("Resolution", selection: $resolution) {
                                ForEach(OutputResolution.allCases) { value in
                                    Text(value.rawValue).tag(value)
                                }
                            }

                            Picker("Target FPS", selection: $targetFPS) {
                                Text("24").tag(24.0)
                                Text("30").tag(30.0)
                                Text("60").tag(60.0)
                            }

                            Picker("Quality", selection: $quality) {
                                Text("Low").tag(QualityPreset.low)
                                Text("Medium").tag(QualityPreset.medium)
                                Text("High").tag(QualityPreset.high)
                            }
                            .disabled(true)

                            Picker("Encoder", selection: $codec) {
                                ForEach(VideoCodec.allCases) { value in
                                    Text(value.rawValue).tag(value)
                                }
                            }
                            .disabled(true)

                            Picker("Orientation", selection: $orientation) {
                                ForEach(CaptureOrientation.allCases) { value in
                                    Text(value.rawValue).tag(value)
                                }
                            }
                        }
                    }

                    HStack {
                        Button(captureService.isRunning ? "Stop" : "Start capture") {
                            if captureService.isRunning {
                                captureService.stop()
                            } else {
                                do {
                                    try captureService.start(request: request)
                                } catch {
                                    // The service publishes the user-visible status.
                                }
                            }
                        }
                        .buttonStyle(.borderedProminent)

                        Text("Quality and Encoder: Stage 2")
                            .font(.caption)
                            .foregroundStyle(.secondary)
                    }

                    GroupBox("Status") {
                        VStack(alignment: .leading, spacing: 6) {
                            statusRow("Status", captureService.status)
                            statusRow("Requested Capability", requestedCapability)
                            statusRow("Native Format", captureService.activeFormatDescription?.summary ?? "Not selected")
                            statusRow("Output", outputDescription)
                            statusRow("Processing", captureService.latestPlan?.processingPath.rawValue ?? "Not selected")
                            statusRow("Target FPS", targetFPS.clean)
                            statusRow("Configured FPS", captureService.activeFrameRateReadback.map { String(format: "%.2f", $0) } ?? "Waiting")
                            statusRow("Actual FPS", String(format: "%.2f", captureService.statisticsSnapshot.fps))
                            statusRow("Dropped Frames", String(captureService.statisticsSnapshot.droppedFrames))
                            statusRow("Pixel Format", captureService.actualPixelFormat?.rawValue ?? "Waiting")
                            statusRow("Transform", captureService.latestPlan.map { "rotate \($0.outputTransform.rotationDegrees)°" } ?? "Not selected")
                        }
                    }

                    GroupBox("Available formats") {
                        if captureService.availableFormats.isEmpty {
                            Text("No camera formats enumerated")
                                .foregroundStyle(.secondary)
                        } else {
                            LazyVStack(alignment: .leading, spacing: 4) {
                                ForEach(captureService.availableFormats) { format in
                                    Text(format.summary)
                                        .font(.caption)
                                }
                            }
                        }
                    }
                }
                .padding()
            }
            .navigationTitle("CamBridge")
            .task {
                if await captureService.requestCameraAccess() {
                    captureService.refreshDevices(position: cameraPosition)
                } else {
                    captureService.refreshDevices(position: cameraPosition)
                }
            }
        }
    }

    private var request: CaptureRequest {
        CaptureRequest(
            cameraPosition: cameraPosition,
            lensID: lensID.isEmpty ? nil : lensID,
            outputResolution: resolution,
            targetFPS: targetFPS,
            orientation: orientation,
            quality: quality,
            codec: codec
        )
    }

    private var outputDescription: String {
        let dimensions = orientation.outputDimensions(for: resolution.dimensions)
        return "\(dimensions.width)x\(dimensions.height), \(orientation.rawValue)"
    }

    private var requestedCapability: String {
        guard !captureService.availableFormats.isEmpty else { return "Not enumerated" }
        let plan = CameraFormatSelector.select(request: request, formats: captureService.availableFormats)
        return plan.status == .supported ? "Supported (\(plan.processingPath.rawValue))" : "Unsupported"
    }

    private func statusRow(_ label: String, _ value: String) -> some View {
        HStack(alignment: .firstTextBaseline) {
            Text(label)
                .frame(width: 120, alignment: .leading)
                .fontWeight(.semibold)
            Text(value)
                .textSelection(.enabled)
        }
    }
}

#Preview {
    ContentView()
}
