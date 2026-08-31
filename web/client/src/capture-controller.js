import {
  snapshotTrackCapabilities,
  snapshotTrackConstraints,
  snapshotTrackSettings,
} from "./capability-probe.js";
import {
  formatCaptureFailure,
  formatCaptureMismatch,
  TEXT,
} from "./i18n.js";
import {
  buildExactVideoConstraints,
  captureDimensions,
  createOutputPlan,
  resolveCaptureOrientation,
} from "./settings.js";

function errorMessage(error) {
  if (error instanceof Error) {
    return error.message;
  }
  return String(error);
}

export function matchesRequestedCapture(settings, actual, captureOrientation = undefined) {
  const dimensions = captureDimensions(settings, captureOrientation);
  return (
    actual.width === dimensions.width &&
    actual.height === dimensions.height &&
    typeof actual.frameRate === "number" &&
    Math.abs(actual.frameRate - settings.frameRate) < 0.5
  );
}

export class CaptureController {
  constructor({
    mediaDevices,
    video,
    meter,
    onMeterUpdate = () => {},
    orientationProvider = defaultOrientationProvider,
  }) {
    this.mediaDevices = mediaDevices;
    this.video = video;
    this.meter = meter;
    this.onMeterUpdate = onMeterUpdate;
    this.orientationProvider = orientationProvider;
    this.stream = null;
    this.track = null;
  }

  async start(settings, deviceId = settings.cameraId) {
    this.stop();
    const captureOrientation = resolveCaptureOrientation(
      settings,
      this.orientationProvider?.() ?? {},
    );
    const requestedSettings = {
      resolution: { ...settings.resolution },
      frameRate: settings.frameRate,
      orientation: settings.orientation,
      quality: settings.quality,
      cameraId: deviceId ?? null,
      captureOrientation,
    };
    const constraints = buildExactVideoConstraints(settings, deviceId, captureOrientation);
    let stream;
    try {
      stream = await this.mediaDevices.getUserMedia(constraints);
    } catch (error) {
      return {
        ok: false,
        status: "unsupported",
        requestedSettings,
        actualSettings: null,
        message: formatCaptureFailure(settings, error),
        exceptionName: error?.name || "Error",
        exceptionMessage: errorMessage(error),
        error: errorMessage(error),
      };
    }

    const [track] = stream.getVideoTracks();
    if (!track) {
      return {
        ok: false,
        status: "error",
        requestedSettings,
        actualSettings: null,
        message: TEXT.captureNoTrack,
        error: "no-video-track",
      };
    }

    const actualSettings = snapshotTrackSettings(track);
    const capabilities = snapshotTrackCapabilities(track);
    const actualConstraints = snapshotTrackConstraints(track);
    if (!matchesRequestedCapture(settings, actualSettings, captureOrientation)) {
      track.stop();
      return {
        ok: false,
        status: "mismatch",
        requestedSettings,
        actualSettings,
        capabilities,
        constraints: actualConstraints,
        message: formatCaptureMismatch(settings),
        error: "settings-mismatch",
      };
    }

    this.stream = stream;
    this.track = track;
    this.video.srcObject = stream;
    if (typeof this.video.play === "function") {
      await this.video.play();
    }
    this.meter.start(this.video, settings.frameRate, this.onMeterUpdate);

    return {
      ok: true,
      status: "running",
      requestedSettings,
      actualSettings,
      capabilities,
      constraints: actualConstraints,
      outputPlan: createOutputPlan({ ...settings, orientation: captureOrientation }),
      stream,
      track,
      message: TEXT.captureRunning,
      error: null,
    };
  }

  stop() {
    this.meter?.stop();
    this.track?.stop();
    this.track = null;
    this.stream = null;
    if (this.video) {
      this.video.srcObject = null;
    }
  }
}

function defaultOrientationProvider() {
  return {
    screenOrientationType: globalThis.screen?.orientation?.type ?? "",
    width: globalThis.innerWidth ?? 0,
    height: globalThis.innerHeight ?? 0,
  };
}
