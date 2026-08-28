import { snapshotTrackCapabilities, snapshotTrackConstraints, snapshotTrackSettings } from "./capability-probe.js";
import { FrameRateMeter } from "./frame-rate-meter.js";

export const MATRIX_CASES = Object.freeze([
  Object.freeze({ width: 1280, height: 720, frameRate: 30 }),
  Object.freeze({ width: 1280, height: 720, frameRate: 60 }),
  Object.freeze({ width: 1920, height: 1080, frameRate: 30 }),
  Object.freeze({ width: 1920, height: 1080, frameRate: 60 }),
  Object.freeze({ width: 2560, height: 1440, frameRate: 30 }),
  Object.freeze({ width: 2560, height: 1440, frameRate: 60 }),
  Object.freeze({ width: 3840, height: 2160, frameRate: 30 }),
  Object.freeze({ width: 3840, height: 2160, frameRate: 60 }),
]);

const CSV_FIELDS = [
  "deviceLabel", "deviceId", "requestedDeviceId", "actualDeviceId", "groupId", "requestedWidth", "requestedHeight", "requestedFPS",
  "getUserMediaSucceeded", "exceptionName", "exceptionMessage",
  "capabilitiesWidth", "capabilitiesHeight", "capabilitiesFrameRateMin", "capabilitiesFrameRateMax",
  "settingsWidth", "settingsHeight", "settingsFrameRate", "settingsDeviceId", "settingsFacingMode", "deviceIdMatches", "constraints",
  "measuredFPS1s", "measuredFPS10s", "missingFrames", "status", "diagnosis",
];

function errorFields(error) {
  return {
    exceptionName: error?.name || error?.constructor?.name || "Error",
    exceptionMessage: error?.message || String(error),
  };
}

export function createDiagnosticRequest(testCase, deviceId) {
  if (!deviceId) {
    throw new TypeError("deviceId is required for diagnostic capture");
  }
  const request = {
    audio: false,
    video: {
      width: { exact: testCase.width },
      height: { exact: testCase.height },
      frameRate: { exact: testCase.frameRate },
    },
  };
  request.video.deviceId = { exact: deviceId };
  return request;
}

export function classifyDiagnosticRow(row) {
  if (!row.getUserMediaSucceeded) {
    return "getUserMedia-failed";
  }
  if (row.deviceIdMatches === false) {
    return "device-mismatch";
  }
  const maxFPS = row.capabilities?.frameRate?.max;
  const actualFPS = row.settings?.frameRate;
  if (row.requestedFPS === 60 && maxFPS < 60 && actualFPS < 60) {
    return "A";
  }
  if (row.requestedFPS === 60 && maxFPS >= 60 && actualFPS < 60) {
    return "B";
  }
  if (row.requestedFPS === 60 && maxFPS >= 60 && actualFPS >= 60 && row.measuredFPS10s < 45) {
    return "C";
  }
  if (row.status === "match") {
    return "match";
  }
  if (actualFPS === row.requestedFPS && (row.measuredFPS10s === null || row.measuredFPS10s >= 45)) {
    return "match";
  }
  return "other-mismatch";
}

export class DiagnosticMatrixRunner {
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
    if (this.video) {
      this.video.srcObject = null;
    }
  }

  async runDevice(device, deviceIndex = 0) {
    this.stopActive();
    const rows = [];
    for (let index = 0; index < MATRIX_CASES.length; index += 1) {
      const row = await this.runCase(device, MATRIX_CASES[index], index, deviceIndex);
      rows.push(row);
      this.onProgress({ kind: "trial-complete", device, index, total: MATRIX_CASES.length, row });
    }
    return rows;
  }

  async runCase(device, testCase, index, deviceIndex = 0) {
    this.stopActive();
    const row = {
      deviceLabel: device.label || `Camera ${deviceIndex + 1}`,
      deviceId: device.deviceId || "",
      requestedDeviceId: device.deviceId || "",
      actualDeviceId: "",
      groupId: device.groupId || "",
      requestedWidth: testCase.width,
      requestedHeight: testCase.height,
      requestedFPS: testCase.frameRate,
      getUserMediaSucceeded: false,
      exceptionName: "",
      exceptionMessage: "",
      capabilities: {},
      settings: {},
      constraints: {},
      measuredFPS1s: null,
      measuredFPS10s: null,
      missingFrames: null,
      status: "pending",
      diagnosis: "pending",
      deviceIdMatches: null,
    };

    let stream;
    try {
      const request = createDiagnosticRequest(testCase, device.deviceId);
      stream = await this.mediaDevices.getUserMedia(request);
    } catch (error) {
      Object.assign(row, errorFields(error), {
        status: device.deviceId ? "getUserMedia-failed" : "device-id-missing",
        diagnosis: device.deviceId ? "getUserMedia-failed" : "device-id-missing",
      });
      return row;
    }

    const [track] = stream.getVideoTracks();
    if (!track) {
      stream.getTracks?.().forEach((candidate) => candidate.stop());
      row.status = "no-video-track";
      row.diagnosis = "no-video-track";
      return row;
    }

    this.activeTrack = track;
    this.video.srcObject = stream;
    row.getUserMediaSucceeded = true;
    try { row.capabilities = snapshotTrackCapabilities(track); } catch (error) { Object.assign(row, errorFields(error)); }
    try { row.settings = snapshotTrackSettings(track); } catch (error) { Object.assign(row, errorFields(error)); }
    try { row.constraints = snapshotTrackConstraints(track); } catch (error) { Object.assign(row, errorFields(error)); }
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
      this.activeMeter.start(this.video, testCase.frameRate, (measurement) => {
        latestMeasurement = measurement;
        this.onProgress({ kind: "measurement", device, index, total: MATRIX_CASES.length, measurement });
      });
    } catch (error) {
      row.measurementError = errorFields(error);
    }

    this.onProgress({ kind: "trial-started", device, index, total: MATRIX_CASES.length, row });
    try {
      await this.wait(10_000);
      row.measuredFPS1s = latestMeasurement.oneSecondFPS ?? null;
      row.measuredFPS10s = latestMeasurement.tenSecondFPS ?? null;
      row.missingFrames = latestMeasurement.missingFrames ?? null;
      row.status = row.deviceIdMatches && row.settings.frameRate === testCase.frameRate ? "match" : "mismatch-observed";
      row.diagnosis = classifyDiagnosticRow(row);
    } catch (error) {
      Object.assign(row, errorFields(error), {
        status: "observation-failed",
        diagnosis: "observation-failed",
      });
    } finally {
      this.stopActive();
    }
    return row;
  }
}

function csvCell(value) {
  if (value === null || value === undefined) return "";
  const text = typeof value === "object" ? JSON.stringify(value) : String(value);
  return /[",\r\n]/.test(text) ? `"${text.replaceAll('"', '""')}"` : text;
}

function rowField(row, field) {
  if (field === "capabilitiesWidth") return row.capabilities?.width;
  if (field === "capabilitiesHeight") return row.capabilities?.height;
  if (field === "capabilitiesFrameRateMin") return row.capabilities?.frameRate?.min;
  if (field === "capabilitiesFrameRateMax") return row.capabilities?.frameRate?.max;
  if (field === "settingsWidth") return row.settings?.width;
  if (field === "settingsHeight") return row.settings?.height;
  if (field === "settingsFrameRate") return row.settings?.frameRate;
  if (field === "settingsDeviceId") return row.settings?.deviceId;
  if (field === "settingsFacingMode") return row.settings?.facingMode;
  return row[field];
}

export function serialiseDiagnosticCSV(rows) {
  const lines = [CSV_FIELDS.join(",")];
  for (const row of rows) {
    lines.push(CSV_FIELDS.map((field) => csvCell(rowField(row, field))).join(","));
  }
  return `${lines.join("\r\n")}\r\n`;
}
