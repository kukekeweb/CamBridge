import test from "node:test";
import assert from "node:assert/strict";
import {
  buildExactVideoConstraints,
  createOutputPlan,
  createSettings,
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
  assert.equal(plan.rotationDegrees, 90);
  assert.equal(plan.transportTransform, "future-stage-2");
});

test("exact request retains an unsupported FPS instead of falling back", () => {
  const settings = createSettings({ frameRate: 60 });
  const constraints = buildExactVideoConstraints(settings);
  const plan = createOutputPlan(settings);

  assert.equal(constraints.video.frameRate.exact, 60);
  assert.equal(plan.requestedFPS, 60);
  assert.equal(plan.fallback, false);
});
