import test from "node:test";
import assert from "node:assert/strict";
import {
  enumerateVideoInputs,
  probeVideoDeviceExposure,
  summariseVideoInputExposure,
  probeCodecCapabilities,
  probeLowLatencyAPIs,
  snapshotTrackCapabilities,
  snapshotTrackConstraints,
  snapshotTrackSettings,
  findActiveCaptureDevice,
} from "../src/capability-probe.js";

test("enumerateVideoInputs preserves returned device fields", async () => {
  const devices = await enumerateVideoInputs({
    async enumerateDevices() {
      return [
        { kind: "videoinput", label: "Back", deviceId: "back", groupId: "group" },
        { kind: "audioinput", label: "Mic", deviceId: "mic" },
        { kind: "videoinput", label: "Front", deviceId: "front", groupId: "" },
      ];
    },
  });

  assert.deepEqual(devices, [
    { kind: "videoinput", label: "Back", deviceId: "back", groupId: "group" },
    { kind: "videoinput", label: "Front", deviceId: "front" },
  ]);
});

test("active capture device resolution uses the priming track deviceId without falling back", () => {
  const devices = [
    { kind: "videoinput", label: "Back", deviceId: "back", groupId: "group-back" },
    { kind: "videoinput", label: "Front", deviceId: "front", groupId: "group-front" },
  ];

  assert.deepEqual(
    findActiveCaptureDevice(devices, { getSettings: () => ({ deviceId: "back", facingMode: "environment" }) }),
    devices[0],
  );
  assert.equal(
    findActiveCaptureDevice(devices, { getSettings: () => ({ deviceId: "" }) }),
    null,
  );
  assert.equal(
    findActiveCaptureDevice(devices, { getSettings: () => ({ deviceId: "unknown" }) }),
    null,
  );
});

test("device exposure probe enumerates while the priming track is live, then records after-stop exposure", async () => {
  const calls = [];
  const permissionTrack = { stopped: false, stop() { this.stopped = true; } };
  let enumerateCount = 0;
  const exposure = await probeVideoDeviceExposure({
    async getUserMedia(request) {
      calls.push(["getUserMedia", request]);
      return { getTracks: () => [permissionTrack] };
    },
    async enumerateDevices() {
      enumerateCount += 1;
      calls.push(["enumerateDevices", permissionTrack.stopped]);
      return enumerateCount === 1
        ? [{ kind: "videoinput", label: "Back", deviceId: "back", groupId: "group" }]
        : [{ kind: "videoinput", label: "", deviceId: "", groupId: "" }];
    },
  });

  assert.deepEqual(exposure.duringActiveCapture, [{ kind: "videoinput", label: "Back", deviceId: "back", groupId: "group" }]);
  assert.deepEqual(exposure.afterPrimingTrackStopped, [{ kind: "videoinput", label: "", deviceId: "", groupId: "" }]);
  assert.deepEqual(calls, [
    ["getUserMedia", { audio: false, video: true }],
    ["enumerateDevices", false],
    ["enumerateDevices", true],
  ]);
  assert.equal(permissionTrack.stopped, true);
  assert.deepEqual(exposure.duringActiveSummary, {
    cameraCount: 1,
    deviceIdsPresent: 1,
    labelsPresent: 1,
    groupIdsPresent: 1,
    entries: [{ index: 1, label: "Back", labelPresent: true, deviceId: "back", deviceIdPresent: true, groupId: "group", groupIdPresent: true }],
  });
  assert.equal(summariseVideoInputExposure(exposure.afterPrimingTrackStopped).deviceIdsPresent, 0);
});

test("track snapshots omit fields the browser did not return", () => {
  const track = {
    getCapabilities: () => ({
      width: { min: 640, max: 1920 },
      frameRate: { min: 1, max: 60 },
      facingMode: ["environment"],
    }),
    getSettings: () => ({ width: 1920, height: 1080, frameRate: 60 }),
    getConstraints: () => ({ width: { exact: 1920 } }),
  };

  assert.deepEqual(snapshotTrackCapabilities(track), {
    width: { min: 640, max: 1920 },
    frameRate: { min: 1, max: 60 },
    facingMode: ["environment"],
  });
  assert.deepEqual(snapshotTrackSettings(track), {
    width: 1920,
    height: 1080,
    frameRate: 60,
  });
  assert.deepEqual(snapshotTrackConstraints(track), { width: { exact: 1920 } });
});

test("codec probe reports only codecs returned by runtime", () => {
  const result = probeCodecCapabilities({
    getCapabilities(kind) {
      assert.equal(kind, "video");
      return {
        codecs: [
          { mimeType: "video/H264", clockRate: 90000, sdpFmtpLine: "level-asymmetry-allowed=1" },
          { mimeType: "video/VP8", clockRate: 90000 },
        ],
      };
    },
  });

  assert.equal(result.available, true);
  assert.deepEqual(result.codecs, [
    { mimeType: "video/H264", clockRate: 90000, sdpFmtpLine: "level-asymmetry-allowed=1" },
    { mimeType: "video/VP8", clockRate: 90000 },
  ]);
});

test("codec and low latency probes report unavailable APIs without inventing support", () => {
  assert.deepEqual(probeCodecCapabilities(undefined), {
    available: false,
    codecs: [],
    error: "RTCRtpSender.getCapabilities is unavailable",
  });

  const receiverPrototype = {
    targetLatency: 0,
    jitterBufferTarget: 0,
    getStats() {},
  };
  const result = probeLowLatencyAPIs({
    RTCRtpReceiver: { prototype: receiverPrototype },
    RTCPeerConnection: function RTCPeerConnection() {},
  });

  assert.deepEqual(result, {
    RTCRtpReceiver: true,
    targetLatency: true,
    jitterBufferTarget: true,
    RTCPeerConnection: true,
    receiverGetStats: true,
  });
});
