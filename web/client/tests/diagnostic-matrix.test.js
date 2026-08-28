import test from "node:test";
import assert from "node:assert/strict";
import {
  DiagnosticMatrixRunner,
  MATRIX_CASES,
  classifyDiagnosticRow,
  createDiagnosticRequest,
  serialiseDiagnosticCSV,
} from "../src/diagnostic-matrix.js";

test("matrix contains all eight resolution and FPS combinations", () => {
  assert.equal(MATRIX_CASES.length, 8);
  assert.deepEqual(
    MATRIX_CASES.map(({ width, height, frameRate }) => `${width}x${height}@${frameRate}`),
    [
      "1280x720@30",
      "1280x720@60",
      "1920x1080@30",
      "1920x1080@60",
      "2560x1440@30",
      "2560x1440@60",
      "3840x2160@30",
      "3840x2160@60",
    ],
  );
});

test("diagnostic requests use exact dimensions, FPS, and selected device", () => {
  assert.deepEqual(createDiagnosticRequest({ width: 1920, height: 1080, frameRate: 60 }, "back"), {
    audio: false,
    video: {
      width: { exact: 1920 },
      height: { exact: 1080 },
      frameRate: { exact: 60 },
      deviceId: { exact: "back" },
    },
  });
});

test("diagnostic classification keeps A, B, and C causes distinct", () => {
  assert.equal(classifyDiagnosticRow({
    getUserMediaSucceeded: true,
    requestedFPS: 60,
    capabilities: { frameRate: { max: 30 } },
    settings: { frameRate: 30 },
  }), "A");
  assert.equal(classifyDiagnosticRow({
    getUserMediaSucceeded: true,
    requestedFPS: 60,
    capabilities: { frameRate: { max: 60 } },
    settings: { frameRate: 30 },
  }), "B");
  assert.equal(classifyDiagnosticRow({
    getUserMediaSucceeded: true,
    requestedFPS: 60,
    capabilities: { frameRate: { max: 60 } },
    settings: { frameRate: 60 },
    measuredFPS10s: 30,
  }), "C");
});

test("runner uses a fresh track for every case and observes mismatch for ten seconds", async () => {
  const tracks = [];
  const constraints = [];
  let waitCount = 0;
  const video = { async play() {} };
  const runner = new DiagnosticMatrixRunner({
    mediaDevices: {
      async getUserMedia(request) {
        constraints.push(request);
        const track = {
          stopped: false,
          getCapabilities: () => ({ frameRate: { min: 1, max: 60 } }),
          getSettings: () => ({ width: request.video.width.exact, height: request.video.height.exact, frameRate: 30 }),
          getConstraints: () => request.video,
          stop() { this.stopped = true; },
        };
        tracks.push(track);
        return { getVideoTracks: () => [track] };
      },
    },
    video,
    meterFactory: () => ({
      start(videoElement, targetFPS, onUpdate) {
        assert.equal(videoElement, video);
        assert.equal(targetFPS, 60);
        onUpdate({ available: true, oneSecondFPS: 30, tenSecondFPS: 30, frameCount: 300, missingFrames: 300 });
      },
      stop() {},
    }),
    wait: async (milliseconds) => {
      assert.equal(milliseconds, 10_000);
      assert.equal(tracks.at(-1).stopped, false);
      waitCount += 1;
    },
  });

  const rows = await runner.runDevice({ label: "Back Camera", deviceId: "back" });

  assert.equal(rows.length, 8);
  assert.equal(waitCount, 8);
  assert.equal(constraints.length, 8);
  assert.equal(rows[3].status, "mismatch-observed");
  assert.equal(rows[3].measuredFPS10s, 30);
  assert.equal(rows[3].diagnosis, "B");
  assert.ok(tracks.every((track) => track.stopped));
  assert.deepEqual(constraints[7].video.deviceId, { exact: "back" });
});

test("diagnostic CSV includes exception and serialized browser snapshots", () => {
  const csv = serialiseDiagnosticCSV([{
    deviceLabel: "Back Camera",
    deviceId: "back",
    requestedWidth: 1920,
    requestedHeight: 1080,
    requestedFPS: 60,
    getUserMediaSucceeded: false,
    exceptionName: "OverconstrainedError",
    exceptionMessage: "width",
    constraints: { width: { exact: 1920 } },
  }]);

  assert.match(csv, /deviceLabel,deviceId,requestedWidth/);
  assert.match(csv, /OverconstrainedError/);
  assert.match(csv, /"\{""width"":\{""exact"":1920\}\}"/);
});

test("runner releases a successful track when the observation timer fails", async () => {
  let track;
  const runner = new DiagnosticMatrixRunner({
    mediaDevices: {
      async getUserMedia() {
        track = {
          getCapabilities: () => ({ frameRate: { min: 1, max: 60 } }),
          getSettings: () => ({ width: 1280, height: 720, frameRate: 30 }),
          getConstraints: () => ({}),
          stop() { this.stopped = true; },
        };
        return { getVideoTracks: () => [track] };
      },
    },
    video: {},
    meterFactory: () => ({ start() {}, stop() {} }),
    wait: async () => { throw new Error("timer failed"); },
  });

  const rows = await runner.runDevice({ label: "Back", deviceId: "back" });

  assert.equal(rows[0].status, "observation-failed");
  assert.equal(rows[0].exceptionMessage, "timer failed");
  assert.equal(track.stopped, true);
});
