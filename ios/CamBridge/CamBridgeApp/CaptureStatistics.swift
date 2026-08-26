import CoreMedia
import Foundation

struct CaptureStatisticsSnapshot: Equatable {
    let deliveredFrames: Int
    let droppedFrames: Int
    let fps: Double
    let dropReasons: [String: Int]
}

struct CaptureStatistics {
    private var timestamps: [Double] = []
    private(set) var deliveredFrames = 0
    private(set) var droppedFrames = 0
    private(set) var dropReasons: [String: Int] = [:]
    private let rollingWindowSeconds = 2.0

    mutating func recordFrame(presentationTimestamp: CMTime) {
        guard presentationTimestamp.isValid else { return }
        let seconds = CMTimeGetSeconds(presentationTimestamp)
        guard seconds.isFinite else { return }
        deliveredFrames += 1
        timestamps.append(seconds)
        prune(through: seconds - rollingWindowSeconds)
    }

    mutating func recordDrop(reason: String) {
        droppedFrames += 1
        dropReasons[reason, default: 0] += 1
    }

    func snapshot(now: Double) -> CaptureStatisticsSnapshot {
        let fps: Double
        if timestamps.count < 2 {
            fps = 0
        } else {
            let deltas = zip(timestamps.dropFirst(), timestamps).map { $0 - $1 }.filter { $0 > 0 }
            if deltas.isEmpty {
                fps = 0
            } else {
                let sorted = deltas.sorted()
                let median = sorted[sorted.count / 2]
                fps = median > 0 ? 1 / median : 0
            }
        }
        _ = now
        return CaptureStatisticsSnapshot(
            deliveredFrames: deliveredFrames,
            droppedFrames: droppedFrames,
            fps: fps,
            dropReasons: dropReasons
        )
    }

    private mutating func prune(through cutoff: Double) {
        guard timestamps.count > 2 else { return }
        let firstRetained = timestamps.firstIndex { $0 >= cutoff } ?? timestamps.count - 1
        if firstRetained > 0 {
            timestamps.removeFirst(firstRetained)
        }
    }
}
