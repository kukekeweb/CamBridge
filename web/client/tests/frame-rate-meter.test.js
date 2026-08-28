import test from "node:test";
import assert from "node:assert/strict";
import {
  FrameRateMeter,
  estimateMissingFrames,
  rateForSamples,
} from "../src/frame-rate-meter.js";

test("rateForSamples calculates a measured rate over a time window", () => {
  const samples = Array.from({ length: 11 }, (_, index) => index * 100);
  assert.equal(rateForSamples(samples, 1), 10);
  assert.equal(rateForSamples(samples, 0.5), 10);
});

test("estimateMissingFrames never returns a negative value", () => {
  assert.equal(estimateMissingFrames(59, 1, 60), 1);
  assert.equal(estimateMissingFrames(60, 1, 60), 0);
  assert.equal(estimateMissingFrames(61, 1, 60), 0);
});

test("FrameRateMeter uses video frame callbacks and reports metadata", () => {
  const callbacks = [];
  const updates = [];
  const video = {
    requestVideoFrameCallback(callback) {
      callbacks.push(callback);
      return callbacks.length;
    },
    cancelVideoFrameCallback() {},
  };
  const meter = new FrameRateMeter();

  meter.start(video, 60, (update) => updates.push(update));
  callbacks.shift()(0, { presentedFrames: 1 });
  callbacks.shift()(1000, { presentedFrames: 60 });

  assert.equal(meter.supported, true);
  assert.equal(updates.at(-1).frameCount, 2);
  assert.equal(updates.at(-1).presentedFrames, 60);
  assert.equal(updates.at(-1).oneSecondFPS, 1);
  assert.equal(updates.at(-1).missingFrames, 58);
  assert.equal(callbacks.length, 1);
});

test("FrameRateMeter marks missing requestVideoFrameCallback as unavailable", () => {
  const updates = [];
  const meter = new FrameRateMeter();
  meter.start({}, 60, (update) => updates.push(update));

  assert.equal(meter.supported, false);
  assert.equal(updates[0].available, false);
});
