import XCTest
import CoreMedia
@testable import CamBridge

final class CaptureStatisticsTests: XCTestCase {
    func testComputesDeliveredFPSFromPresentationTimestamps() {
        var stats = CaptureStatistics()
        for index in 0..<60 {
            stats.recordFrame(presentationTimestamp: CMTime(value: CMTimeValue(index), timescale: 60))
        }
        let snapshot = stats.snapshot(now: 1.0)
        XCTAssertEqual(snapshot.deliveredFrames, 60)
        XCTAssertEqual(snapshot.fps, 60, accuracy: 0.01)
    }

    func testCountsDroppedFramesAndPreservesReason() {
        var stats = CaptureStatistics()
        stats.recordDrop(reason: "frameWasLate")
        stats.recordDrop(reason: "frameWasLate")
        let snapshot = stats.snapshot(now: 1.0)
        XCTAssertEqual(snapshot.droppedFrames, 2)
        XCTAssertEqual(snapshot.dropReasons["frameWasLate"], 2)
    }
}
