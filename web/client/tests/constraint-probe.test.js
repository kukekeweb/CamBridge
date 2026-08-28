import test from "node:test";
import assert from "node:assert/strict";
import {
  ConstraintProbeRunner,
  classifyConstraintProbeRow,
  createBaseCameraRequest,
  createFrameRateOnlyRequest,
  createMinIdealFrameRateRequest,
  createResolutionConstraintRequest,
} from "../src/constraint-probe.js";

test("constraint probe requests always identify the selected device", () => {
  assert.deepEqual(createFrameRateOnlyRequest("back"), {
    audio: false,
    video: { deviceId: { exact: "back" }, frameRate: { exact: 60 } },
  });
  assert.deepEqual(createBaseCameraRequest("back"), {
    audio: false,
    video: { deviceId: { exact: "back" } },
  });
  assert.deepEqual(createMinIdealFrameRateRequest("back"), {
    audio: false,
    video: { deviceId: { exact: "back" }, frameRate: { min: 60, ideal: 60 } },
  });
  assert.deepEqual(createResolutionConstraintRequest("back", 1920, 1080), {
    audio: false,
    video: {
      deviceId: { exact: "back" },
      width: { exact: 1920 },
      height: { exact: 1080 },
      frameRate: { exact: 60 },
    },
  });
});

test("constraint probe classifies actual 60, measured 60, and actual 30 separately", () => {
  assert.equal(classifyConstraintProbeRow({ getUserMediaSucceeded: false }), "error");
  assert.equal(classifyConstraintProbeRow({
    getUserMediaSucceeded: true,
    settings: { frameRate: 60 },
    measuredFPS10s: 60,
  }), "success-60");
  assert.equal(classifyConstraintProbeRow({
    getUserMediaSucceeded: true,
    settings: { frameRate: 60 },
    measuredFPS10s: 30,
  }), "track-60-measured-low");
  assert.equal(classifyConstraintProbeRow({
    getUserMediaSucceeded: true,
    settings: { frameRate: 30 },
    measuredFPS10s: 30,
  }), "actual-below-60");
});

test("constraint probe runs fresh exact, applyConstraints, and min/ideal trials", async () => {
  const requests = [];
  const tracks = [];
  const runner = new ConstraintProbeRunner({
    mediaDevices: {
      async getUserMedia(request) {
        requests.push(request);
        const track = {
          stopped: false,
          getCapabilities: () => ({ frameRate: { min: 1, max: 60 } }),
          getSettings: () => ({ width: 1920, height: 1080, frameRate: 30, deviceId: "back", facingMode: "environment" }),
          getConstraints: () => request.video,
          async applyConstraints(constraints) {
            track.applied = constraints;
          },
          stop() { track.stopped = true; },
        };
        tracks.push(track);
        return { getVideoTracks: () => [track] };
      },
    },
    video: { async play() {} },
    meterFactory: () => ({
      start(video, targetFPS, onUpdate) {
        assert.equal(targetFPS, 60);
        onUpdate({ available: true, oneSecondFPS: 30, tenSecondFPS: 30, frameCount: 300, missingFrames: 300 });
      },
      stop() {},
    }),
    wait: async (milliseconds) => assert.equal(milliseconds, 10_000),
  });

  const rows = await runner.runDevice({ label: "Back Camera", deviceId: "back", groupId: "group" });

  assert.equal(rows.length, 3);
  assert.deepEqual(requests[0].video.deviceId, { exact: "back" });
  assert.deepEqual(requests[1].video.deviceId, { exact: "back" });
  assert.deepEqual(tracks[1].applied, { frameRate: { exact: 60 } });
  assert.deepEqual(requests[2].video.frameRate, { min: 60, ideal: 60 });
  assert.ok(tracks.every((track) => track.stopped));
  assert.ok(rows.every((row) => row.deviceIdMatches === true));
  assert.ok(rows.every((row) => row.diagnosis === "actual-below-60"));
});
