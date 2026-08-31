import test from "node:test";
import assert from "node:assert/strict";
import { CaptureController, matchesRequestedCapture } from "../src/capture-controller.js";
import { createSettings } from "../src/settings.js";

function fakeTrack(settings) {
  return {
    stopped: false,
    getCapabilities: () => ({ width: { min: 640, max: 3840 } }),
    getSettings: () => settings,
    getConstraints: () => ({ width: { exact: settings.width } }),
    stop() {
      this.stopped = true;
    },
  };
}

test("capture rejection is reported as unavailable without retry", async () => {
  let calls = 0;
  const controller = new CaptureController({
    mediaDevices: {
      async getUserMedia() {
        calls += 1;
        throw new Error("Permission denied");
      },
    },
    video: {},
    meter: { start() {}, stop() {} },
  });

  const result = await controller.start(createSettings(), "back");

  assert.equal(calls, 1);
  assert.equal(result.ok, false);
  assert.equal(result.status, "unsupported");
  assert.match(result.message, /1920×1080 \/ 60fpsは利用できません/);
});

test("settings mismatch stops the track and never falls back", async () => {
  const track = fakeTrack({ width: 1920, height: 1080, frameRate: 30 });
  let calls = 0;
  const controller = new CaptureController({
    mediaDevices: {
      async getUserMedia(constraints) {
        calls += 1;
        assert.equal(constraints.video.frameRate.exact, 60);
        return { getVideoTracks: () => [track] };
      },
    },
    video: {},
    meter: { start() {}, stop() {} },
  });

  const result = await controller.start(createSettings(), "back");

  assert.equal(calls, 1);
  assert.equal(track.stopped, true);
  assert.equal(result.ok, false);
  assert.equal(result.status, "mismatch");
  assert.deepEqual(result.actualSettings, { width: 1920, height: 1080, frameRate: 30 });
  assert.match(result.message, /実際のカメラ設定が要求値と一致しません/);
});

test("successful capture attaches the stream and starts empirical measurement", async () => {
  const track = fakeTrack({ width: 1920, height: 1080, frameRate: 60 });
  const stream = { getVideoTracks: () => [track] };
  const video = { srcObject: null, async play() {} };
  let meterArguments;
  const controller = new CaptureController({
    mediaDevices: { async getUserMedia() { return stream; } },
    video,
    meter: {
      start(...args) {
        meterArguments = args;
      },
      stop() {},
    },
  });

  const result = await controller.start(createSettings(), "back");

  assert.equal(result.ok, true);
  assert.equal(result.status, "running");
  assert.equal(video.srcObject, stream);
  assert.equal(meterArguments[1], 60);
});

test("portrait capture accepts an exact 1080x1920 track at 60fps", () => {
  const settings = createSettings({
    orientation: "portrait",
    resolution: { width: 1920, height: 1080 },
    frameRate: 60,
  });

  assert.equal(matchesRequestedCapture(settings, { width: 1080, height: 1920, frameRate: 60 }), true);
  assert.equal(matchesRequestedCapture(settings, { width: 1920, height: 1080, frameRate: 60 }), false);
});

test("auto orientation requests and accepts the viewport's portrait layout", async () => {
  const track = fakeTrack({ width: 1080, height: 1920, frameRate: 60 });
  const stream = { getVideoTracks: () => [track] };
  const video = { srcObject: null, async play() {} };
  const controller = new CaptureController({
    mediaDevices: {
      async getUserMedia(constraints) {
        assert.deepEqual(constraints.video.width, { exact: 1080 });
        assert.deepEqual(constraints.video.height, { exact: 1920 });
        return stream;
      },
    },
    video,
    meter: { start() {}, stop() {} },
    orientationProvider: () => ({ screenOrientationType: "portrait-primary", width: 390, height: 844 }),
  });

  const result = await controller.start(createSettings({ orientation: "auto" }), "back");

  assert.equal(result.ok, true);
  assert.equal(result.requestedSettings.captureOrientation, "portrait");
});
