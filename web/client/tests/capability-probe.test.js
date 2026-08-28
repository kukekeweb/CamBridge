import test from "node:test";
import assert from "node:assert/strict";
import {
  enumerateVideoInputs,
  probeCodecCapabilities,
  probeLowLatencyAPIs,
  snapshotTrackCapabilities,
  snapshotTrackConstraints,
  snapshotTrackSettings,
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
