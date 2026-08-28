import {
  snapshotTrackCapabilities,
  snapshotTrackConstraints,
  snapshotTrackSettings,
} from "./capability-probe.js";
import { estimateMissingFrames, rateForSamples } from "./frame-rate-meter.js";
import { buildExactVideoConstraints } from "./settings.js";

export const STABILITY_TEST_DURATION_MS = 600_000;
export const STABILITY_SCHEMA_VERSION = 1;

const DEFAULT_CLOCK = Object.freeze({
  now: () => Date.now(),
  setTimeout: (callback, delay) => setTimeout(callback, delay),
  clearTimeout: (timer) => clearTimeout(timer),
});

function errorFields(error) {
  return {
    name: error?.name || error?.constructor?.name || "Error",
    message: error?.message || String(error),
  };
}

function exactSettingsMatch(requested, actual = {}) {
  actual ??= {};
  return (
    actual.width === requested.width &&
    actual.height === requested.height &&
    typeof actual.frameRate === "number" &&
    Math.abs(actual.frameRate - requested.frameRate) < 0.5
  );
}

function average(values) {
  if (values.length === 0) return null;
  return values.reduce((sum, value) => sum + value, 0) / values.length;
}

function minimum(values) {
  return values.length === 0 ? null : Math.min(...values);
}

function isoTime(milliseconds) {
  return new Date(milliseconds).toISOString();
}

function clone(value) {
  if (value === undefined) return undefined;
  return JSON.parse(JSON.stringify(value));
}

export function evaluateStabilityReport(report) {
  const reasons = [];
  if (!report.completed || report.elapsedSeconds < STABILITY_TEST_DURATION_MS / 1000) {
    reasons.push("600秒完走なし");
  }
  if (!exactSettingsMatch(report.requested, report.actual) || !exactSettingsMatch(report.requested, report.finalActual)) {
    reasons.push("実際の解像度またはFPSが要求値を維持していない");
  }
  if (report.mismatchCount !== 0) reasons.push("途中の設定不一致あり");
  if ((report.track?.endedEventCount ?? 0) !== 0) reasons.push("Track endedイベントあり");
  if ((report.cameraReacquisitionCount ?? 0) !== 0) reasons.push("意図しないカメラ再取得あり");
  if ((report.javascriptErrorCount ?? 0) !== 0) reasons.push("JavaScript errorあり");
  if ((report.unhandledRejectionCount ?? 0) !== 0) reasons.push("unhandledrejectionあり");
  if ((report.totalVideoFrames ?? 0) === 0) reasons.push("video frameを取得できていない");
  if (report.track?.readyStateAtCompletion && report.track.readyStateAtCompletion !== "live") {
    reasons.push("観察終了時のTrackがliveではない");
  }
  if (report.videoFrameCallbackSupported === false) reasons.push("requestVideoFrameCallback非対応");
  if (report.videoFrameCallbackStopped) reasons.push("requestVideoFrameCallbackが途中停止");

  return {
    status: reasons.length === 0 ? "PASS" : "FAIL",
    reasons,
  };
}

export class StabilityTestRunner {
  constructor({
    mediaDevices,
    video,
    clock = DEFAULT_CLOCK,
    eventTarget = globalThis,
    documentTarget = globalThis.document ?? globalThis,
    durationMs = STABILITY_TEST_DURATION_MS,
    onUpdate = () => {},
  }) {
    this.mediaDevices = mediaDevices;
    this.video = video;
    this.clock = clock;
    this.eventTarget = eventTarget;
    this.documentTarget = documentTarget;
    this.durationMs = durationMs;
    this.onUpdate = onUpdate;
    this._active = null;
  }

  async start(settings, deviceId = settings.cameraId) {
    if (this._active) throw new Error("A stability test is already running");

    const startedAtMs = this.clock.now();
    const requested = {
      width: settings.resolution.width,
      height: settings.resolution.height,
      frameRate: settings.frameRate,
    };
    const state = {
      startedAtMs,
      requested,
      deviceId: deviceId || "",
      stream: null,
      track: null,
      timer: null,
      requestId: null,
      resolve: null,
      running: true,
      stopReason: null,
      actual: null,
      finalActual: null,
      capabilities: {},
      constraints: {},
      frameTimestamps: [],
      totalVideoFrames: 0,
      nextOneSecondSample: 1,
      oneSecondFPS: [],
      tenSecondFPS: [],
      settingsChanges: [],
      mismatchCount: 0,
      errors: [],
      javascriptErrorCount: 0,
      unhandledRejectionCount: 0,
      videoFrameCallbackSupported: typeof this.video?.requestVideoFrameCallback === "function",
      videoFrameCallbackStopped: false,
      trackEndedEventCount: 0,
      trackMuteEventCount: 0,
      trackUnmuteEventCount: 0,
      pageLifecycle: {
        visibilityChangeCount: 0,
        pagehideCount: 0,
        pageshowCount: 0,
        backgroundIntervals: [],
        activeBackgroundStartedAtMs: null,
      },
      cameraReacquisitionCount: 0,
      getUserMediaCallCount: 0,
      listeners: [],
    };
    this._active = state;

    let stream;
    try {
      state.getUserMediaCallCount += 1;
      stream = await this.mediaDevices.getUserMedia(
        buildExactVideoConstraints(settings, deviceId),
      );
    } catch (error) {
      state.errors.push({ type: "getUserMedia", ...errorFields(error) });
      return this._finish("getUserMedia-error");
    }

    const [track] = stream.getVideoTracks?.() ?? [];
    if (!track) {
      state.errors.push({ type: "track", name: "NoVideoTrack", message: "No video track" });
      stream.getTracks?.().forEach((candidate) => candidate.stop());
      return this._finish("no-video-track");
    }

    state.stream = stream;
    state.track = track;
    this.video.srcObject = stream;
    state.actual = snapshotTrackSettings(track);
    state.finalActual = clone(state.actual);
    state.capabilities = snapshotTrackCapabilities(track);
    state.constraints = snapshotTrackConstraints(track);
    this._recordSettings(state, state.actual, true);
    this._installListeners(state);

    try {
      await this.video.play?.();
    } catch (error) {
      state.errors.push({ type: "video-play", ...errorFields(error) });
      return this._finish("video-play-error");
    }

    if (!state.videoFrameCallbackSupported) {
      return this._finish("request-video-frame-callback-unavailable");
    }

    state.timer = this.clock.setTimeout(() => this._finish("duration"), this.durationMs);
    const onFrame = (timestamp) => {
      if (!state.running) return;
      try {
        const frameTimestamp = typeof timestamp === "number" ? timestamp : this.clock.now();
        state.frameTimestamps.push(frameTimestamp);
        state.totalVideoFrames += 1;
        const elapsedSeconds = Math.max(0, (this.clock.now() - state.startedAtMs) / 1000);
        const actual = snapshotTrackSettings(track);
        this._recordSettings(state, actual, false);
        this._recordSamples(state, elapsedSeconds, frameTimestamp);
        this.onUpdate(this._liveSummary(state, elapsedSeconds));
        if (state.running) state.requestId = this.video.requestVideoFrameCallback(onFrame);
      } catch (error) {
        state.errors.push({ type: "video-frame-callback", ...errorFields(error) });
        state.videoFrameCallbackStopped = true;
        this._finish("video-frame-callback-error");
      }
    };

    try {
      state.requestId = this.video.requestVideoFrameCallback(onFrame);
    } catch (error) {
      state.errors.push({ type: "video-frame-callback", ...errorFields(error) });
      state.videoFrameCallbackStopped = true;
      return this._finish("video-frame-callback-error");
    }
    this.onUpdate(this._liveSummary(state, 0));
    return new Promise((resolve) => {
      state.resolve = resolve;
    });
  }

  stop(reason = "user") {
    if (this._active?.running) this._finish(reason);
  }

  _installListeners(state) {
    const add = (target, type, listener) => {
      if (typeof target?.addEventListener !== "function") return;
      target.addEventListener(type, listener);
      state.listeners.push({ target, type, listener });
    };
    const record = (type, error) => {
      state.errors.push({ type, ...errorFields(error), elapsedSeconds: this._elapsed(state) });
    };
    add(state.track, "ended", () => {
      state.trackEndedEventCount += 1;
    });
    add(state.track, "mute", () => {
      state.trackMuteEventCount += 1;
    });
    add(state.track, "unmute", () => {
      state.trackUnmuteEventCount += 1;
    });
    add(this.eventTarget, "error", (event) => {
      state.javascriptErrorCount += 1;
      record("javascript-error", event);
    });
    add(this.eventTarget, "unhandledrejection", (event) => {
      state.unhandledRejectionCount += 1;
      record("unhandled-rejection", event?.reason ?? event);
    });
    add(this.documentTarget, "visibilitychange", () => {
      state.pageLifecycle.visibilityChangeCount += 1;
      const hidden = this.documentTarget.visibilityState === "hidden";
      if (hidden && state.pageLifecycle.activeBackgroundStartedAtMs === null) {
        state.pageLifecycle.activeBackgroundStartedAtMs = this.clock.now();
      } else if (!hidden && state.pageLifecycle.activeBackgroundStartedAtMs !== null) {
        this._closeBackgroundInterval(state, this.clock.now());
      }
    });
    add(this.eventTarget, "pagehide", () => {
      state.pageLifecycle.pagehideCount += 1;
      if (state.pageLifecycle.activeBackgroundStartedAtMs === null) {
        state.pageLifecycle.activeBackgroundStartedAtMs = this.clock.now();
      }
    });
    add(this.eventTarget, "pageshow", () => {
      state.pageLifecycle.pageshowCount += 1;
      if (state.pageLifecycle.activeBackgroundStartedAtMs !== null) {
        this._closeBackgroundInterval(state, this.clock.now());
      }
    });
  }

  _recordSettings(state, actual, initial) {
    if (!exactSettingsMatch(state.requested, actual)) state.mismatchCount += 1;
    const previous = initial ? null : state.finalActual;
    if (!initial && JSON.stringify(previous) !== JSON.stringify(actual)) {
      state.settingsChanges.push({
        elapsedSeconds: this._elapsed(state),
        settings: clone(actual),
      });
    }
    state.finalActual = clone(actual);
  }

  _recordSamples(state, elapsedSeconds, frameTimestamp) {
    while (elapsedSeconds >= state.nextOneSecondSample) {
      const oneSecondFPS = rateForSamples(state.frameTimestamps, 1);
      state.oneSecondFPS.push({
        elapsedSeconds: state.nextOneSecondSample,
        fps: oneSecondFPS,
        frameCount: state.totalVideoFrames,
      });
      if (state.nextOneSecondSample >= 10) {
        state.tenSecondFPS.push({
          elapsedSeconds: state.nextOneSecondSample,
          fps: rateForSamples(state.frameTimestamps, 10),
          frameCount: state.totalVideoFrames,
        });
      }
      state.nextOneSecondSample += 1;
    }
    void frameTimestamp;
  }

  _closeBackgroundInterval(state, endedAtMs) {
    const startedAtMs = state.pageLifecycle.activeBackgroundStartedAtMs;
    if (startedAtMs === null) return;
    state.pageLifecycle.backgroundIntervals.push({
      startedAt: isoTime(startedAtMs),
      endedAt: isoTime(endedAtMs),
      durationSeconds: (endedAtMs - startedAtMs) / 1000,
    });
    state.pageLifecycle.activeBackgroundStartedAtMs = null;
  }

  _elapsed(state) {
    return Math.max(0, (this.clock.now() - state.startedAtMs) / 1000);
  }

  _liveSummary(state, elapsedSeconds) {
    const oneSecondValues = state.oneSecondFPS.map((sample) => sample.fps);
    const tenSecondValues = state.tenSecondFPS.map((sample) => sample.fps);
    return {
      elapsedSeconds,
      totalVideoFrames: state.totalVideoFrames,
      currentFPS: rateForSamples(state.frameTimestamps, 1),
      minOneSecondFPS: minimum(oneSecondValues),
      averageOneSecondFPS: average(oneSecondValues),
      trackReadyState: state.track?.readyState ?? "unknown",
      requestedFPSDeficiency: estimateMissingFrames(state.totalVideoFrames, elapsedSeconds, state.requested.frameRate),
      minTenSecondFPS: minimum(tenSecondValues),
      averageTenSecondFPS: average(tenSecondValues),
    };
  }

  _finish(reason) {
    const state = this._active;
    if (!state) return Promise.resolve(null);
    state.running = false;
    state.stopReason = reason;
    if (state.timer !== null) this.clock.clearTimeout(state.timer);
    if (this.video && state.requestId !== null && typeof this.video.cancelVideoFrameCallback === "function") {
      this.video.cancelVideoFrameCallback(state.requestId);
    }
    state.requestId = null;
    for (const { target, type, listener } of state.listeners) {
      target.removeEventListener?.(type, listener);
    }
    state.listeners = [];
    if (state.pageLifecycle.activeBackgroundStartedAtMs !== null) {
      this._closeBackgroundInterval(state, this.clock.now());
    }
    state.finalActual = state.track ? snapshotTrackSettings(state.track) : state.finalActual;
    const elapsedSeconds = this._elapsed(state);
    const values1 = state.oneSecondFPS.map((sample) => sample.fps);
    const values10 = state.tenSecondFPS.map((sample) => sample.fps);
    const report = {
      schemaVersion: STABILITY_SCHEMA_VERSION,
      testType: "1080p60-stability",
      requested: clone(state.requested),
      actual: clone(state.actual),
      finalActual: clone(state.finalActual),
      capabilities: clone(state.capabilities),
      constraints: clone(state.constraints),
      startedAt: isoTime(state.startedAtMs),
      endedAt: isoTime(this.clock.now()),
      elapsedSeconds,
      completed: reason === "duration",
      stopReason: reason,
      totalVideoFrames: state.totalVideoFrames,
      oneSecondFPS: clone(state.oneSecondFPS),
      tenSecondFPS: clone(state.tenSecondFPS),
      minOneSecondFPS: minimum(values1),
      maxOneSecondFPS: values1.length ? Math.max(...values1) : null,
      averageOneSecondFPS: average(values1),
      minTenSecondFPS: minimum(values10),
      averageTenSecondFPS: average(values10),
      requestedFPSDeficiency: estimateMissingFrames(state.totalVideoFrames, elapsedSeconds, state.requested.frameRate),
      track: {
        readyStateAtCompletion: state.track?.readyState ?? "unavailable",
        readyStateAfterCleanup: "unavailable",
        endedEventCount: state.trackEndedEventCount,
        muteEventCount: state.trackMuteEventCount,
        unmuteEventCount: state.trackUnmuteEventCount,
      },
      settingsChanges: clone(state.settingsChanges),
      mismatchCount: state.mismatchCount,
      videoFrameCallbackSupported: state.videoFrameCallbackSupported,
      videoFrameCallbackStopped: state.videoFrameCallbackStopped,
      javascriptErrorCount: state.javascriptErrorCount,
      unhandledRejectionCount: state.unhandledRejectionCount,
      javascriptErrors: clone(state.errors),
      pageLifecycle: clone(state.pageLifecycle),
      cameraReacquisitionCount: Math.max(0, state.getUserMediaCallCount - 1),
      getUserMediaCallCount: state.getUserMediaCallCount,
    };
    state.track?.stop();
    report.track.readyStateAfterCleanup = state.track?.readyState ?? "unavailable";
    Object.assign(report, evaluateStabilityReport(report));
    this.video.srcObject = null;
    this._active = null;
    this.onUpdate({ ...this._liveSummary(state, elapsedSeconds), completed: true, report });
    if (state.resolve) state.resolve(report);
    return report;
  }
}
