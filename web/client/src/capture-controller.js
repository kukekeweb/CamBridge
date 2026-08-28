import {
  snapshotTrackCapabilities,
  snapshotTrackConstraints,
  snapshotTrackSettings,
} from "./capability-probe.js";
import { buildExactVideoConstraints, createOutputPlan } from "./settings.js";

function errorMessage(error) {
  if (error instanceof Error) {
    return error.message;
  }
  return String(error);
}

function requestLabel(settings) {
  const { width, height } = settings.resolution;
  if (width === 1920 && height === 1080 && settings.frameRate === 60) {
    return "1080p60";
  }
  return `${width}x${height}@${settings.frameRate}`;
}

function matchesRequested(settings, actual) {
  return (
    actual.width === settings.resolution.width &&
    actual.height === settings.resolution.height &&
    typeof actual.frameRate === "number" &&
    Math.abs(actual.frameRate - settings.frameRate) < 0.5
  );
}

export class CaptureController {
  constructor({ mediaDevices, video, meter, onMeterUpdate = () => {} }) {
    this.mediaDevices = mediaDevices;
    this.video = video;
    this.meter = meter;
    this.onMeterUpdate = onMeterUpdate;
    this.stream = null;
    this.track = null;
  }

  async start(settings, deviceId = settings.cameraId) {
    this.stop();
    const requestedSettings = {
      resolution: { ...settings.resolution },
      frameRate: settings.frameRate,
      orientation: settings.orientation,
      quality: settings.quality,
      cameraId: deviceId ?? null,
    };
    const constraints = buildExactVideoConstraints(settings, deviceId);
    let stream;
    try {
      stream = await this.mediaDevices.getUserMedia(constraints);
    } catch (error) {
      return {
        ok: false,
        status: "unsupported",
        requestedSettings,
        actualSettings: null,
        message: `${requestLabel(settings)} unavailable: ${errorMessage(error)}`,
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
        message: "Camera stream returned no video track",
        error: "no-video-track",
      };
    }

    const actualSettings = snapshotTrackSettings(track);
    const capabilities = snapshotTrackCapabilities(track);
    const actualConstraints = snapshotTrackConstraints(track);
    if (!matchesRequested(settings, actualSettings)) {
      track.stop();
      return {
        ok: false,
        status: "mismatch",
        requestedSettings,
        actualSettings,
        capabilities,
        constraints: actualConstraints,
        message: `${requestLabel(settings)} unavailable: actual track does not match requested settings`,
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
      outputPlan: createOutputPlan(settings),
      stream,
      track,
      message: "Capture running",
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
