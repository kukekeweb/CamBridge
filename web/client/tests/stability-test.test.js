import test from "node:test";
import assert from "node:assert/strict";
import {
  STABILITY_TEST_DURATION_MS,
  StabilityTestRunner,
  evaluateStabilityReport,
} from "../src/stability-test.js";
import { createSettings } from "../src/settings.js";

class FakeClock {
  constructor() {
    this.time = 0;
    this.timers = [];
  }

  now = () => this.time;

  setTimeout = (callback, delay) => {
    const timer = { callback, due: this.time + delay, cleared: false };
    this.timers.push(timer);
    return timer;
  };

  clearTimeout = (timer) => {
    if (timer) timer.cleared = true;
  };

  advance(milliseconds) {
    this.time += milliseconds;
    for (const timer of this.timers.filter((candidate) => !candidate.cleared && candidate.due <= this.time)) {
      timer.cleared = true;
      timer.callback();
    }
  }
}

class FakeEventTarget {
  constructor() {
    this.listeners = new Map();
    this.visibilityState = "visible";
  }

  addEventListener(type, listener) {
    const listeners = this.listeners.get(type) ?? [];
    listeners.push(listener);
    this.listeners.set(type, listeners);
  }

  removeEventListener(type, listener) {
    this.listeners.set(type, (this.listeners.get(type) ?? []).filter((candidate) => candidate !== listener));
  }

  dispatch(type, event = {}) {
    for (const listener of this.listeners.get(type) ?? []) listener(event);
  }
}

function fakeVideo() {
  const callbacks = [];
  return {
    callbacks,
    srcObject: null,
    requestVideoFrameCallback(callback) {
      callbacks.push(callback);
      return callbacks.length;
    },
    cancelVideoFrameCallback() {},
    async play() {},
  };
}

function fakeTrack() {
  const events = new FakeEventTarget();
  const settings = { width: 1920, height: 1080, frameRate: 60, deviceId: "rear" };
  return {
    readyState: "live",
    stopped: false,
    getCapabilities: () => ({ frameRate: { min: 1, max: 60 } }),
    getSettings: () => ({ ...settings }),
    getConstraints: () => ({ width: { exact: 1920 }, height: { exact: 1080 }, frameRate: { exact: 60 } }),
    setSettings(next) {
      Object.assign(settings, next);
    },
    addEventListener: events.addEventListener.bind(events),
    removeEventListener: events.removeEventListener.bind(events),
    emit: events.dispatch.bind(events),
    stop() {
      this.stopped = true;
      this.readyState = "ended";
    },
  };
}

async function startRunner({ clock = new FakeClock(), eventTarget = new FakeEventTarget(), track = fakeTrack(), onUpdate } = {}) {
  const video = fakeVideo();
  const runner = new StabilityTestRunner({
    mediaDevices: {
      async getUserMedia() {
        return { getVideoTracks: () => [track], getTracks: () => [track] };
      },
    },
    video,
    clock,
    eventTarget,
    documentTarget: eventTarget,
    onUpdate,
  });
  const reportPromise = runner.start(createSettings({ cameraId: "rear" }), "rear");
  await Promise.resolve();
  return { clock, eventTarget, track, video, runner, reportPromise };
}

test("stability runner completes 600 seconds with fake clock and collects FPS aggregates", async () => {
  const updates = [];
  const context = await startRunner({ onUpdate: (update) => updates.push(update) });
  for (let second = 1; second <= 10; second += 1) {
    context.clock.advance(1000);
    context.video.callbacks.shift()(second * 1000, { presentedFrames: second * 60 });
  }
  context.clock.advance(STABILITY_TEST_DURATION_MS - 10_000);
  const report = await context.reportPromise;

  assert.equal(report.completed, true);
  assert.equal(report.elapsedSeconds, 600);
  assert.deepEqual(report.requested, { width: 1920, height: 1080, frameRate: 60 });
  assert.equal(report.actual.width, 1920);
  assert.equal(report.actual.height, 1080);
  assert.equal(report.actual.frameRate, 60);
  assert.equal(report.track.endedEventCount, 0);
  assert.equal(report.cameraReacquisitionCount, 0);
  assert.equal(report.javascriptErrorCount, 0);
  assert.equal(report.unhandledRejectionCount, 0);
  assert.equal(report.oneSecondFPS.length, 10);
  assert.ok(updates.length > 0);
  assert.equal(context.track.stopped, true);
});

test("stability runner records track events, page lifecycle, background interval, and errors", async () => {
  const context = await startRunner();
  context.eventTarget.visibilityState = "hidden";
  context.eventTarget.dispatch("visibilitychange");
  context.eventTarget.dispatch("pagehide");
  context.eventTarget.dispatch("error", { message: "synthetic error" });
  context.eventTarget.dispatch("unhandledrejection", { reason: "synthetic rejection" });
  context.track.emit("mute");
  context.track.emit("unmute");
  context.track.emit("ended");
  context.clock.advance(5000);
  context.eventTarget.visibilityState = "visible";
  context.eventTarget.dispatch("visibilitychange");
  context.eventTarget.dispatch("pageshow");
  context.runner.stop("user");
  const report = await context.reportPromise;

  assert.equal(report.track.endedEventCount, 1);
  assert.equal(report.track.muteEventCount, 1);
  assert.equal(report.track.unmuteEventCount, 1);
  assert.equal(report.javascriptErrorCount, 1);
  assert.equal(report.unhandledRejectionCount, 1);
  assert.equal(report.pageLifecycle.pagehideCount, 1);
  assert.equal(report.pageLifecycle.pageshowCount, 1);
  assert.equal(report.pageLifecycle.backgroundIntervals[0].durationSeconds, 5);
  assert.equal(evaluateStabilityReport(report).status, "FAIL");
});

test("stability runner records an intermediate getSettings change as a mismatch", async () => {
  const context = await startRunner();
  context.clock.advance(1000);
  context.video.callbacks.shift()(1000);
  context.track.setSettings({ frameRate: 30 });
  context.clock.advance(1000);
  context.video.callbacks.shift()(2000);
  context.runner.stop("user");
  const report = await context.reportPromise;

  assert.equal(report.settingsChanges.length, 1);
  assert.equal(report.settingsChanges[0].settings.frameRate, 30);
  assert.ok(report.mismatchCount > 0);
});

test("stability evaluation fails when no video frame was observed", () => {
  const report = {
    completed: true,
    elapsedSeconds: 600,
    requested: { width: 1920, height: 1080, frameRate: 60 },
    actual: { width: 1920, height: 1080, frameRate: 60 },
    finalActual: { width: 1920, height: 1080, frameRate: 60 },
    totalVideoFrames: 36_000,
    totalVideoFrames: 0,
    mismatchCount: 0,
    cameraReacquisitionCount: 0,
    track: { endedEventCount: 0, readyStateAtCompletion: "live" },
    javascriptErrorCount: 0,
    unhandledRejectionCount: 0,
    videoFrameCallbackSupported: true,
    videoFrameCallbackStopped: false,
  };

  assert.equal(evaluateStabilityReport(report).status, "FAIL");
});

test("stability PASS does not fail for a transient one-second FPS dip", () => {
  const report = {
    completed: true,
    elapsedSeconds: 600,
    totalVideoFrames: 36_000,
    requested: { width: 1920, height: 1080, frameRate: 60 },
    actual: { width: 1920, height: 1080, frameRate: 60 },
    finalActual: { width: 1920, height: 1080, frameRate: 60 },
    mismatchCount: 0,
    cameraReacquisitionCount: 0,
    track: { endedEventCount: 0, readyStateAtCompletion: "live" },
    javascriptErrorCount: 0,
    unhandledRejectionCount: 0,
    oneSecondFPS: [{ fps: 59 }, { fps: 60 }],
    videoFrameCallbackSupported: true,
    videoFrameCallbackStopped: false,
  };

  assert.equal(evaluateStabilityReport(report).status, "PASS");
});

test("stability report JSON has stable export fields", async () => {
  const context = await startRunner();
  context.runner.stop("user");
  const report = await context.reportPromise;

  assert.equal(report.schemaVersion, 1);
  for (const field of [
    "requested", "actual", "startedAt", "endedAt", "elapsedSeconds", "totalVideoFrames",
    "oneSecondFPS", "tenSecondFPS", "minOneSecondFPS", "averageOneSecondFPS",
    "minTenSecondFPS", "averageTenSecondFPS", "requestedFPSDeficiency", "track",
    "settingsChanges", "mismatchCount", "videoFrameCallbackStopped", "javascriptErrorCount",
    "unhandledRejectionCount", "pageLifecycle", "cameraReacquisitionCount", "status",
  ]) {
    assert.ok(Object.hasOwn(report, field), field);
  }
  assert.equal(typeof JSON.stringify(report), "string");
});
