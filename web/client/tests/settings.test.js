import test from "node:test";
import assert from "node:assert/strict";
import {
  buildExactVideoConstraints,
  captureDimensions,
  createOutputPlan,
  createSettings,
  resolveCaptureOrientation,
  shouldRestartCaptureForOrientationChange,
} from "../src/settings.js";

test("buildExactVideoConstraints uses exact resolution and frame rate", async () => {
  const { buildExactVideoConstraints } = await import("../src/settings.js");
  const settings = createSettings({
    resolution: { width: 1920, height: 1080 },
    frameRate: 60,
  });

  assert.deepEqual(buildExactVideoConstraints(settings, "back-camera"), {
    audio: false,
    video: {
      width: { exact: 1920 },
      height: { exact: 1080 },
      frameRate: { exact: 60 },
      deviceId: { exact: "back-camera" },
    },
  });
});

test("createOutputPlan swaps dimensions for portrait transport intent", () => {
  const plan = createOutputPlan(createSettings({
    orientation: "portrait",
    resolution: { width: 2560, height: 1440 },
  }));

  assert.deepEqual(plan.requestedDimensions, { width: 2560, height: 1440 });
  assert.deepEqual(plan.outputDimensions, { width: 1440, height: 2560 });
  assert.equal(plan.rotationDegrees, 0);
  assert.equal(plan.transportTransform, "identity");
});

test("exact request retains an unsupported FPS instead of falling back", () => {
  const settings = createSettings({ frameRate: 60 });
  const constraints = buildExactVideoConstraints(settings);
  const plan = createOutputPlan(settings);

  assert.equal(constraints.video.frameRate.exact, 60);
  assert.equal(plan.requestedFPS, 60);
  assert.equal(plan.fallback, false);
});

test("portrait capture requests the exact dimensions with width and height swapped", () => {
  const settings = createSettings({
    orientation: "portrait",
    resolution: { width: 1920, height: 1080 },
    frameRate: 60,
  });

  assert.deepEqual(captureDimensions(settings), { width: 1080, height: 1920 });
  assert.deepEqual(buildExactVideoConstraints(settings).video, {
    width: { exact: 1080 },
    height: { exact: 1920 },
    frameRate: { exact: 60 },
  });
});

test("orientation is explicit and does not follow the viewport", () => {
  const settings = createSettings();
  assert.equal(settings.orientation, "landscape");
  assert.equal(createSettings({ orientation: "auto" }).orientation, "landscape");
  assert.equal(resolveCaptureOrientation(settings, {
    screenOrientationType: "portrait-primary",
    width: 390,
    height: 844,
  }), "landscape");
  assert.deepEqual(captureDimensions(settings), { width: 1920, height: 1080 });
  assert.deepEqual(buildExactVideoConstraints(settings).video, {
    width: { exact: 1920 },
    height: { exact: 1080 },
    frameRate: { exact: 60 },
  });
});

test("orientation selection requests a reacquire only for an active capture", () => {
  assert.equal(shouldRestartCaptureForOrientationChange(
    createSettings({ orientation: "landscape" }),
    createSettings({ orientation: "portrait" }),
    true,
  ), true);
  assert.equal(shouldRestartCaptureForOrientationChange(
    createSettings({ orientation: "landscape" }),
    createSettings({ orientation: "portrait" }),
    false,
  ), false);
  assert.equal(shouldRestartCaptureForOrientationChange(
    createSettings({ orientation: "landscape" }),
    createSettings({ orientation: "landscape" }),
    true,
  ), false);
});
