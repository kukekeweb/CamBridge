import { snapshotTrackCapabilities, snapshotTrackConstraints, snapshotTrackSettings } from "./capability-probe.js";
import { FrameRateMeter } from "./frame-rate-meter.js";

const TARGET_FPS = 60;

function requireDeviceId(deviceId) {
  if (!deviceId) {
    throw new TypeError("deviceId is required for 60fps constraint probe");
  }
}

export function createFrameRateOnlyRequest(deviceId) {
  requireDeviceId(deviceId);
  return { audio: false, video: { deviceId: { exact: deviceId }, frameRate: { exact: TARGET_FPS } } };
}

export function createBaseCameraRequest(deviceId) {
  requireDeviceId(deviceId);
  return { audio: false, video: { deviceId: { exact: deviceId } } };
}

export function createMinIdealFrameRateRequest(deviceId) {
  requireDeviceId(deviceId);
  return { audio: false, video: { deviceId: { exact: deviceId }, frameRate: { min: TARGET_FPS, ideal: TARGET_FPS } } };
}

function createResolutionStepRequest(deviceId, dimension, value, width, height) {
  requireDeviceId(deviceId);
  return {
    audio: false,
    video: {
      deviceId: { exact: deviceId },
      ...(dimension === "width" ? { width: { exact: value } } : {}),
      ...(dimension === "height" ? { height: { exact: value } } : {}),
      ...(width !== undefined ? { width: { exact: width } } : {}),
      ...(height !== undefined ? { height: { exact: height } } : {}),
      frameRate: { exact: TARGET_FPS },
    },
  };
}

export function createWidthConstraintRequest(deviceId, width) {
  return createResolutionStepRequest(deviceId, "width", width);
}

export function createHeightConstraintRequest(deviceId, height) {
  return createResolutionStepRequest(deviceId, "height", height);
}

export function createResolutionConstraintRequest(deviceId, width, height) {
  return createResolutionStepRequest(deviceId, "both", undefined, width, height);
}

export function classifyConstraintProbeRow(row) {
  if (!row.getUserMediaSucceeded || row.applyConstraintsSucceeded === false) return "error";
  if (row.deviceIdMatches === false) return "device-mismatch";
  if (row.settings?.frameRate >= TARGET_FPS && row.measuredFPS10s >= 45) return "success-60";
  if (row.settings?.frameRate >= TARGET_FPS) return "track-60-measured-low";
  return "actual-below-60";
}

function errorFields(error) {
  return {
    exceptionName: error?.name || error?.constructor?.name || "Error",
    exceptionMessage: error?.message || String(error),
  };
}

function snapshot(track, method, fallback) {
  try {
    return method(track);
  } catch {
    return fallback;
  }
}

const PRIMARY_PROBES = Object.freeze([
  Object.freeze({ id: "gum-frame-rate-exact", label: "frameRateのみ exact 60", request: createFrameRateOnlyRequest }),
  Object.freeze({ id: "apply-constraints-exact", label: "基本Track後 applyConstraints exact 60", request: createBaseCameraRequest, applyConstraints: { frameRate: { exact: TARGET_FPS } } }),
  Object.freeze({ id: "gum-frame-rate-min-ideal", label: "frameRate min / ideal 60", request: createMinIdealFrameRateRequest }),
]);

export class ConstraintProbeRunner {
  constructor({ mediaDevices, video, meterFactory = () => new FrameRateMeter(), wait, onProgress = () => {} }) {
    this.mediaDevices = mediaDevices;
    this.video = video;
    this.meterFactory = meterFactory;
    this.wait = wait ?? ((milliseconds) => new Promise((resolve) => setTimeout(resolve, milliseconds)));
    this.onProgress = onProgress;
    this.activeTrack = null;
    this.activeMeter = null;
  }

  stopActive() {
    this.activeMeter?.stop();
    this.activeTrack?.stop();
    this.activeMeter = null;
    this.activeTrack = null;
    if (this.video) this.video.srcObject = null;
  }

  async runDevice(device, deviceIndex = 0) {
    this.stopActive();
    const rows = [];
    for (const probe of PRIMARY_PROBES) {
      const row = await this.runProbe(device, probe, deviceIndex);
      rows.push(row);
      this.onProgress({ kind: "trial-complete", device, deviceIndex, row });
    }

    const successfulResolutions = new Map();
    for (const row of rows) {
      if (row.diagnosis === "success-60" && row.settings.width && row.settings.height) {
        successfulResolutions.set(`${row.settings.width}x${row.settings.height}`, {
          width: row.settings.width,
          height: row.settings.height,
        });
      }
    }
    for (const { width, height } of successfulResolutions.values()) {
      const augmented = [
        { id: `width-${width}`, label: `width追加 ${width} + exact 60`, request: () => createWidthConstraintRequest(device.deviceId, width) },
        { id: `height-${height}`, label: `height追加 ${height} + exact 60`, request: () => createHeightConstraintRequest(device.deviceId, height) },
        { id: `resolution-${width}x${height}`, label: `width + height追加 ${width}×${height} + exact 60`, request: () => createResolutionConstraintRequest(device.deviceId, width, height) },
      ];
      for (const probe of augmented) {
        const row = await this.runProbe(device, probe, deviceIndex);
        rows.push(row);
        this.onProgress({ kind: "trial-complete", device, deviceIndex, row });
      }
    }
    return rows;
  }

  async runProbe(device, probe, deviceIndex) {
    this.stopActive();
    const row = {
      deviceLabel: device.label || `Camera ${deviceIndex + 1}`,
      deviceId: device.deviceId || "",
      requestedDeviceId: device.deviceId || "",
      actualDeviceId: "",
      groupId: device.groupId || "",
      probeId: probe.id,
      probeLabel: probe.label,
      request: null,
      applyConstraints: probe.applyConstraints ?? null,
      getUserMediaSucceeded: false,
      applyConstraintsSucceeded: probe.applyConstraints ? null : true,
      exceptionName: "",
      exceptionMessage: "",
      capabilities: {},
      initialSettings: {},
      settings: {},
      constraints: {},
      measuredFPS1s: null,
      measuredFPS10s: null,
      missingFrames: null,
      deviceIdMatches: null,
      status: "pending",
      diagnosis: "pending",
    };

    if (!device.deviceId) {
      Object.assign(row, {
        status: "device-id-missing",
        diagnosis: "error",
        exceptionName: "TypeError",
        exceptionMessage: "deviceId is required for 60fps constraint probe",
      });
      return row;
    }
    const request = probe.request(device.deviceId);
    row.request = request;

    let stream;
    try {
      stream = await this.mediaDevices.getUserMedia(request);
    } catch (error) {
      Object.assign(row, errorFields(error), { status: "getUserMedia-failed", diagnosis: "error" });
      return row;
    }

    const [track] = stream.getVideoTracks();
    if (!track) {
      stream.getTracks?.().forEach((candidate) => candidate.stop());
      return { ...row, status: "no-video-track", diagnosis: "error" };
    }
    this.activeTrack = track;
    this.video.srcObject = stream;
    row.getUserMediaSucceeded = true;
    row.initialSettings = snapshot(track, snapshotTrackSettings, {});
    row.capabilities = snapshot(track, snapshotTrackCapabilities, {});

    if (probe.applyConstraints) {
      try {
        await track.applyConstraints(probe.applyConstraints);
        row.applyConstraintsSucceeded = true;
      } catch (error) {
        Object.assign(row, errorFields(error));
        row.applyConstraintsSucceeded = false;
      }
    }

    row.settings = snapshot(track, snapshotTrackSettings, {});
    row.constraints = snapshot(track, snapshotTrackConstraints, {});
    row.actualDeviceId = row.settings.deviceId || "";
    row.deviceIdMatches = row.settings.deviceId === device.deviceId;
    try {
      await this.video.play?.();
    } catch (error) {
      row.previewError = errorFields(error);
    }

    let latestMeasurement = {};
    this.activeMeter = this.meterFactory();
    try {
      this.activeMeter.start(this.video, TARGET_FPS, (measurement) => {
        latestMeasurement = measurement;
        this.onProgress({ kind: "measurement", device, deviceIndex, row, measurement });
      });
    } catch (error) {
      row.measurementError = errorFields(error);
    }

    this.onProgress({ kind: "trial-started", device, deviceIndex, row });
    try {
      await this.wait(10_000);
      row.measuredFPS1s = latestMeasurement.oneSecondFPS ?? null;
      row.measuredFPS10s = latestMeasurement.tenSecondFPS ?? null;
      row.missingFrames = latestMeasurement.missingFrames ?? null;
      row.status = "observed";
      row.diagnosis = classifyConstraintProbeRow(row);
    } catch (error) {
      Object.assign(row, errorFields(error), { status: "observation-failed", diagnosis: "error" });
    } finally {
      this.stopActive();
    }
    return row;
  }
}

const CSV_FIELDS = [
  "deviceLabel", "deviceId", "requestedDeviceId", "actualDeviceId", "groupId", "probeId", "probeLabel", "request", "applyConstraints",
  "getUserMediaSucceeded", "applyConstraintsSucceeded", "exceptionName", "exceptionMessage",
  "capabilities", "initialSettings", "settings", "constraints", "measuredFPS1s", "measuredFPS10s",
  "missingFrames", "deviceIdMatches", "status", "diagnosis",
];

function csvCell(value) {
  if (value === null || value === undefined) return "";
  const text = typeof value === "object" ? JSON.stringify(value) : String(value);
  return /[",\r\n]/.test(text) ? `"${text.replaceAll('"', '""')}"` : text;
}

export function serialiseConstraintProbeCSV(rows) {
  return `${[CSV_FIELDS.join(","), ...rows.map((row) => CSV_FIELDS.map((field) => csvCell(row[field])).join(","))].join("\r\n")}\r\n`;
}
