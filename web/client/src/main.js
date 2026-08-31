import {
  enumerateVideoInputs,
  enumerateVideoInputExposure,
  findActiveCaptureDevice,
  probeVideoDeviceExposure,
  probeCodecCapabilities,
  probeLowLatencyAPIs,
} from "./capability-probe.js";
import { CaptureController } from "./capture-controller.js";
import {
  DiagnosticMatrixRunner,
  serialiseDiagnosticCSV,
} from "./diagnostic-matrix.js";
import {
  ConstraintProbeRunner,
  serialiseConstraintProbeCSV,
} from "./constraint-probe.js";
import { FrameRateMeter } from "./frame-rate-meter.js";
import { StabilityTestRunner } from "./stability-test.js";
import { WebRtcSender, formatWebRtcTrackRequirementError } from "./webrtc-sender.js";
import {
  describeError,
  formatActualCapture,
  formatCapabilities,
  formatCameraLabel,
  formatCodecProbeError,
  formatConstraintProbeResult,
  formatConstraints,
  formatDiagnosticResult,
  formatLatencyAPIs,
  formatMeasuredFPS,
  formatOutputPlan,
  formatRequestedCapture,
  formatSettings,
  formatStabilityFPS,
  formatStabilitySeconds,
  formatStabilityStatus,
  formatWebRtcStatus,
  formatWebRtcStats,
  TEXT,
} from "./i18n.js";
import {
  createOutputPlan,
  createSettings,
  RESOLUTIONS,
} from "./settings.js";

const $ = (id) => document.getElementById(id);
const video = $("camera-preview");
const errors = [];
let devices = [];
let latestSettings = createSettings();
let matrixRows = [];
let constraintProbeRows = [];
let stabilityRunning = false;
let stabilityReport = null;
let webRtcSender = null;
const webRtcSessionId = `cambridge-${globalThis.crypto?.randomUUID?.() ?? `${Date.now()}-${Math.random().toString(16).slice(2)}`}`;

function json(value) {
  return JSON.stringify(value, null, 2);
}

function recordError(error) {
  const message = error instanceof Error || error?.name ? describeError(error) : String(error);
  errors.push(message);
  $("errors-output").textContent = json(errors);
}

function renderWebRtcStatus(status) {
  const element = $("webrtc-status");
  element.textContent = formatWebRtcStatus(status);
  element.className = status === "connected"
    ? "status running"
    : typeof status === "string" && status.startsWith("error:")
      ? "status error"
      : "status";
}

function renderWebRtcStats(stats) {
  $("webrtc-stats").textContent = formatWebRtcStats(stats);
}

function signalingUrl() {
  if (globalThis.location?.protocol !== "https:") return null;
  return `wss://${globalThis.location.host}/signaling`;
}

function closeWebRtcSender() {
  webRtcSender?.close();
  webRtcSender = null;
  $("connect-webrtc-button").disabled = !controller.track;
  $("disconnect-webrtc-button").disabled = true;
  renderWebRtcStats({ available: false });
}

function renderRequested(settings) {
  $("requested-value").textContent = formatRequestedCapture(settings);
  const plan = createOutputPlan(settings);
  $("output-plan-value").textContent = formatOutputPlan(plan);
}

function populateCameraSelect() {
  const select = $("camera-select");
  const previous = select.value;
  select.replaceChildren();
  if (devices.length === 0) {
    select.disabled = true;
    select.append(new Option(TEXT.noVideoInputs, ""));
    return;
  }
  if (!devices.some((device) => device.deviceId)) {
    select.disabled = true;
    select.append(new Option(TEXT.cameraSelectionPending, ""));
    return;
  }
  select.disabled = false;
  devices.forEach((device, index) => {
    const label = formatCameraLabel(device.label, index);
    select.append(new Option(label, device.deviceId || ""));
  });
  if ([...select.options].some((option) => option.value === previous)) {
    select.value = previous;
  }
}

async function refreshDevices() {
  if (!navigator.mediaDevices?.enumerateDevices) {
    recordError(TEXT.enumerateUnavailable);
    return;
  }
  try {
    devices = await enumerateVideoInputs(navigator.mediaDevices);
    populateCameraSelect();
    $("devices-output").textContent = json(devices);
  } catch (error) {
    recordError(error);
  }
}

function renderCodecs() {
  const result = probeCodecCapabilities(globalThis.RTCRtpSender);
  const list = $("codec-list");
  list.replaceChildren();
  if (!result.available) {
    const item = document.createElement("li");
    item.textContent = formatCodecProbeError(result.error);
    list.append(item);
    return;
  }
  if (result.codecs.length === 0) {
    const item = document.createElement("li");
    item.textContent = TEXT.noCodecs;
    list.append(item);
    return;
  }
  result.codecs.forEach((codec) => {
    const item = document.createElement("li");
    const details = [codec.mimeType, codec.clockRate && `${codec.clockRate} Hz`, codec.channels && `${codec.channels} ch`, codec.sdpFmtpLine].filter(Boolean);
    item.textContent = details.join(" · ");
    list.append(item);
  });
}

function readSettings() {
  const [width, height] = $("resolution-select").value.split("x").map(Number);
  return createSettings({
    cameraId: $("camera-select").value || null,
    orientation: $("orientation-select").value,
    resolution: RESOLUTIONS.find((resolution) => resolution.width === width && resolution.height === height) ?? { width, height },
    frameRate: Number($("frame-rate-select").value),
    quality: $("quality-select").value,
  });
}

function renderMeter(update) {
  if (!update.available) {
    $("measured-value").textContent = formatMeasuredFPS(update);
    $("frame-counter-value").textContent = TEXT.noValue;
    $("missing-value").textContent = TEXT.noValue;
    return;
  }
  $("measured-value").textContent = formatMeasuredFPS(update);
  $("frame-counter-value").textContent = String(update.frameCount);
  $("missing-value").textContent = String(update.missingFrames);
}

function renderStability(update) {
  const report = update.report;
  $("stability-elapsed-value").textContent = `${formatStabilitySeconds(update.elapsedSeconds)} / 10:00`;
  $("stability-current-fps-value").textContent = formatStabilityFPS(update.currentFPS);
  $("stability-min-fps-value").textContent = formatStabilityFPS(update.minOneSecondFPS);
  $("stability-average-fps-value").textContent = formatStabilityFPS(update.averageOneSecondFPS);
  $("stability-track-value").textContent = update.trackReadyState || TEXT.noValue;
  if (!report) {
    $("stability-status").textContent = TEXT.stabilityRunning;
    $("stability-status").className = "status running";
    return;
  }
  stabilityReport = report;
  const status = formatStabilityStatus(report.status);
  $("stability-status").textContent = TEXT.stabilityComplete.replace("{status}", status);
  $("stability-status").className = report.status === "PASS" ? "status running" : "status error";
  $("stability-export").value = JSON.stringify(report, null, 2);
  $("copy-stability-json-button").disabled = false;
}

function renderMatrix() {
  const body = $("matrix-table-body");
  body.replaceChildren();
  for (const row of matrixRows) {
    const tableRow = document.createElement("tr");
    const values = [
      row.deviceLabel || row.deviceId || TEXT.unknownCamera,
      formatRequestedCapture({ resolution: { width: row.requestedWidth, height: row.requestedHeight }, frameRate: row.requestedFPS }),
      row.getUserMediaSucceeded ? TEXT.matrixSuccess : TEXT.matrixErrorResult,
      row.settings?.width ? formatActualCapture(row.settings) : TEXT.noValue,
      row.settings?.facingMode || TEXT.noValue,
      row.measuredFPS10s === null || row.measuredFPS10s === undefined ? TEXT.noValue : `${row.measuredFPS10s.toFixed(2)} fps`,
      formatDiagnosticResult(row.diagnosis),
    ];
    values.forEach((value, index) => {
      const cell = document.createElement("td");
      cell.textContent = String(value);
      if (index === 2 && !row.getUserMediaSucceeded) cell.className = "matrix-failed";
      if (index === 6 && row.diagnosis !== "match") cell.className = "matrix-mismatch";
      if (index === 6 && row.diagnosis === "match") cell.className = "matrix-match";
      tableRow.append(cell);
    });
    body.append(tableRow);
  }
  const exportArea = $("matrix-export");
  exportArea.value = JSON.stringify(matrixRows, null, 2);
  $("copy-json-button").disabled = matrixRows.length === 0;
  $("copy-csv-button").disabled = matrixRows.length === 0;
}

function renderConstraintProbe() {
  const body = $("constraint-probe-table-body");
  body.replaceChildren();
  for (const row of constraintProbeRows) {
    const values = [
      row.deviceLabel || row.deviceId || TEXT.unknownCamera,
      row.probeLabel,
      row.getUserMediaSucceeded ? TEXT.matrixSuccess : TEXT.matrixErrorResult,
      row.applyConstraints === null ? TEXT.noValue : row.applyConstraintsSucceeded ? TEXT.matrixSuccess : TEXT.matrixErrorResult,
      row.settings?.width ? formatActualCapture(row.settings) : TEXT.noValue,
      row.settings?.facingMode || TEXT.noValue,
      row.measuredFPS10s === null || row.measuredFPS10s === undefined ? TEXT.noValue : `${row.measuredFPS10s.toFixed(2)} fps`,
      formatConstraintProbeResult(row.diagnosis),
    ];
    const tableRow = document.createElement("tr");
    values.forEach((value, index) => {
      const cell = document.createElement("td");
      cell.textContent = String(value);
      if ((index === 2 || index === 3) && value === TEXT.matrixErrorResult) cell.className = "matrix-failed";
      if (index === 7 && row.diagnosis === "success-60") cell.className = "matrix-match";
      if (index === 7 && row.diagnosis !== "success-60") cell.className = "matrix-mismatch";
      tableRow.append(cell);
    });
    body.append(tableRow);
  }
  $("constraint-probe-export").value = JSON.stringify(constraintProbeRows, null, 2);
  $("copy-constraint-json-button").disabled = constraintProbeRows.length === 0;
  $("copy-constraint-csv-button").disabled = constraintProbeRows.length === 0;
}

async function copyMatrix(text, format) {
  try {
    await navigator.clipboard.writeText(text);
    $("matrix-status").textContent = TEXT.clipboardCopied.replace("{format}", format);
  } catch (error) {
    const message = TEXT.clipboardFailed.replace("{message}", describeError(error));
    $("matrix-status").textContent = `● ${message}`;
    recordError(error);
  }
}

const meter = new FrameRateMeter();
const controller = new CaptureController({
  mediaDevices: navigator.mediaDevices,
  video,
  meter,
  onMeterUpdate: renderMeter,
});

const matrixRunner = new DiagnosticMatrixRunner({
  mediaDevices: navigator.mediaDevices,
  video,
  meterFactory: () => new FrameRateMeter(),
  onProgress: (progress) => {
    if (progress.kind === "trial-started") {
      const camera = progress.device.label || progress.device.deviceId || TEXT.unknownCamera;
      $("matrix-status").textContent = TEXT.matrixObserving
        .replace("{camera}", camera)
        .replace("{width}", progress.row.requestedWidth)
        .replace("{height}", progress.row.requestedHeight)
        .replace("{fps}", progress.row.requestedFPS);
    }
  },
});

const constraintProbeRunner = new ConstraintProbeRunner({
  mediaDevices: navigator.mediaDevices,
  video,
  meterFactory: () => new FrameRateMeter(),
  onProgress: (progress) => {
    if (progress.kind === "trial-started") {
      const camera = progress.device.label || progress.device.deviceId || TEXT.unknownCamera;
      $("constraint-probe-status").textContent = TEXT.constraintObserving
        .replace("{camera}", camera)
        .replace("{probe}", progress.row.probeLabel);
    }
  },
});

const stabilityRunner = new StabilityTestRunner({
  mediaDevices: navigator.mediaDevices,
  video,
  onUpdate: renderStability,
});

async function refreshDevicesAfterPermission() {
  if (!navigator.mediaDevices?.getUserMedia || !navigator.mediaDevices?.enumerateDevices) {
    throw new Error(TEXT.enumerateUnavailable);
  }
  const exposure = await probeVideoDeviceExposure(navigator.mediaDevices);
  devices = exposure.duringActiveCapture;
  populateCameraSelect();
  $("devices-output").textContent = json(devices);
  $("exposure-output").textContent = json({
    duringActiveCapture: exposure.duringActiveSummary,
    afterPrimingTrackStopped: exposure.afterStoppedSummary,
  });
  if (!devices.some((device) => device.deviceId)) {
    throw { name: "DeviceIdentityUnavailable", message: "", exposure };
  }
  return devices;
}

async function primeRearCameraForSelection() {
  const primingStream = await navigator.mediaDevices.getUserMedia({
    audio: false,
    video: { facingMode: { exact: "environment" } },
  });
  const [primingTrack] = primingStream.getVideoTracks?.() ?? [];
  if (!primingTrack) {
    primingStream.getTracks?.().forEach((track) => track.stop());
    throw { name: "NotFoundError", message: TEXT.captureNoTrack };
  }

  try {
    const activeDevices = await enumerateVideoInputExposure(navigator.mediaDevices);
    const selectedDevice = findActiveCaptureDevice(activeDevices, primingTrack);
    devices = activeDevices;
    populateCameraSelect();
    $("devices-output").textContent = json(devices);

    if (!selectedDevice) {
      throw { name: "DeviceIdentityUnavailable", message: "" };
    }

    $("camera-select").value = selectedDevice.deviceId;
    latestSettings = readSettings();
    renderRequested(latestSettings);
    return selectedDevice.deviceId;
  } finally {
    primingStream.getTracks?.().forEach((track) => track.stop());
  }
}

function selectedDeviceFrom(refreshedDevices, selectedId) {
  return refreshedDevices.find((device) => device.deviceId === selectedId) || refreshedDevices[0];
}

function selectDiagnosticDevices(refreshedDevices, selectedId, runAll) {
  const identifiedDevices = refreshedDevices.filter((device) => device.deviceId);
  if (runAll) return identifiedDevices.map((device) => ({ device, index: refreshedDevices.indexOf(device) }));
  const effectiveSelectedId = selectedId || $("camera-select").value;
  const device = selectedDeviceFrom(identifiedDevices, effectiveSelectedId);
  const index = refreshedDevices.indexOf(device);
  return device ? [{ device, index }] : [];
}

$("latency-output").textContent = formatLatencyAPIs(probeLowLatencyAPIs(globalThis));
$("webrtc-session-id").textContent = webRtcSessionId;
$("webrtc-url").textContent = signalingUrl() ?? "HTTPSで開いてください";
renderWebRtcStatus("idle");
renderCodecs();
renderRequested(latestSettings);
refreshDevices();

$("start-button").addEventListener("click", async () => {
  closeWebRtcSender();
  $("capture-status").textContent = TEXT.captureStarting;
  $("capture-status").className = "status";
  $("start-button").disabled = true;
  try {
    let cameraId = $("camera-select").value || null;
    if (!cameraId) {
      $("capture-status").textContent = TEXT.cameraSelectionPriming;
      cameraId = await primeRearCameraForSelection();
    }

    const settings = readSettings();
    latestSettings = settings;
    renderRequested(settings);
    const result = await controller.start(settings, cameraId);
    $("settings-output").textContent = formatSettings(result.actualSettings ?? {});
    $("capabilities-output").textContent = formatCapabilities(result.capabilities ?? {});
    $("constraints-output").textContent = formatConstraints(result.constraints ?? {});
    $("actual-value").textContent = result.actualSettings
      ? formatActualCapture(result.actualSettings)
      : TEXT.noValue;
    if (result.ok) {
      $("capture-status").textContent = TEXT.captureRunning;
      $("capture-status").className = "status running";
      $("preview-placeholder").hidden = true;
      $("connect-webrtc-button").disabled = false;
      await refreshDevices();
    } else {
      $("capture-status").textContent = `● ${result.message}`;
      $("capture-status").className = "status error";
      recordError(result.message);
      $("connect-webrtc-button").disabled = true;
    }
  } catch (error) {
    $("capture-status").textContent = `● ${describeError(error)}`;
    $("capture-status").className = "status error";
    recordError(error);
    $("connect-webrtc-button").disabled = true;
  } finally {
    $("start-button").disabled = false;
  }
});

$("stop-button").addEventListener("click", () => {
  closeWebRtcSender();
  controller.stop();
  $("preview-placeholder").hidden = false;
  $("capture-status").textContent = TEXT.captureStopped;
  $("capture-status").className = "status";
  $("actual-value").textContent = TEXT.noValue;
});

$("connect-webrtc-button").addEventListener("click", async () => {
  if (!controller.track || !controller.stream) {
    const error = new Error(formatWebRtcTrackRequirementError(controller.track, controller.stream));
    renderWebRtcStatus("error: " + error.message);
    recordError(error);
    return;
  }
  const url = signalingUrl();
  if (!url) {
    const error = new Error(TEXT.webrtcRequiresHttps);
    renderWebRtcStatus("error: " + error.message);
    recordError(error);
    return;
  }

  webRtcSender?.close();
  webRtcSender = new WebRtcSender({
    signalingUrl: url,
    sessionId: webRtcSessionId,
    track: controller.track,
    stream: controller.stream,
    onStatus: renderWebRtcStatus,
    onStats: renderWebRtcStats,
  });
  $("connect-webrtc-button").disabled = true;
  $("disconnect-webrtc-button").disabled = false;
  try {
    await webRtcSender.connect();
  } catch (error) {
    $("connect-webrtc-button").disabled = false;
    $("disconnect-webrtc-button").disabled = true;
    renderWebRtcStatus("error: " + (error instanceof Error ? error.message : describeError(error)));
    recordError(error);
  }
});

$("disconnect-webrtc-button").addEventListener("click", () => {
  closeWebRtcSender();
});

$("start-stability-button").addEventListener("click", async () => {
  if (stabilityRunning) return;
  const cameraId = $("camera-select").value;
  if (!cameraId) {
    $("stability-status").textContent = TEXT.stabilityNoCamera;
    $("stability-status").className = "status error";
    return;
  }
  closeWebRtcSender();
  controller.stop();
  stabilityRunning = true;
  stabilityReport = null;
  $("stability-status").textContent = TEXT.stabilityStarting;
  $("stability-status").className = "status";
  $("stability-export").value = "";
  $("start-stability-button").disabled = true;
  $("stop-stability-button").disabled = false;
  $("run-matrix-button").disabled = true;
  $("run-all-matrix-button").disabled = true;
  $("run-constraint-probe-button").disabled = true;
  $("run-all-constraint-probe-button").disabled = true;
  try {
    const settings = createSettings({
      cameraId,
      resolution: { width: 1920, height: 1080 },
      frameRate: 60,
    });
    await stabilityRunner.start(settings, cameraId);
  } catch (error) {
    $("stability-status").textContent = `● ${describeError(error)}`;
    $("stability-status").className = "status error";
    recordError(error);
  } finally {
    stabilityRunning = false;
    $("start-stability-button").disabled = false;
    $("stop-stability-button").disabled = true;
    $("run-matrix-button").disabled = false;
    $("run-all-matrix-button").disabled = false;
    $("run-constraint-probe-button").disabled = false;
    $("run-all-constraint-probe-button").disabled = false;
  }
});

$("stop-stability-button").addEventListener("click", () => {
  if (stabilityRunning) stabilityRunner.stop("user");
});

$("copy-stability-json-button").addEventListener("click", async () => {
  if (!stabilityReport) return;
  try {
    await navigator.clipboard.writeText(JSON.stringify(stabilityReport, null, 2));
    $("stability-status").textContent = TEXT.stabilityCopied;
  } catch (error) {
    const message = TEXT.clipboardFailed.replace("{message}", describeError(error));
    $("stability-status").textContent = `● ${message}`;
    recordError(error);
  }
});

async function runMatrix(runAll) {
  const selectedId = $("camera-select").value;
  let refreshedDevices;
  try {
    refreshedDevices = await refreshDevicesAfterPermission();
  } catch (error) {
    $("matrix-status").textContent = TEXT.matrixError.replace("{message}", describeError(error));
    recordError(error);
    return;
  }
  const targetDevices = selectDiagnosticDevices(refreshedDevices, selectedId, runAll);
  if (targetDevices.length === 0) {
    $("matrix-status").textContent = TEXT.matrixNoInputs;
    return;
  }
  closeWebRtcSender();
  controller.stop();
  $("preview-placeholder").hidden = false;
  matrixRows = [];
  renderMatrix();
  $("run-matrix-button").disabled = true;
  $("run-all-matrix-button").disabled = true;
  $("run-constraint-probe-button").disabled = true;
  $("run-all-constraint-probe-button").disabled = true;
  try {
    for (const { device, index } of targetDevices) {
      const rows = await matrixRunner.runDevice(device, index);
      matrixRows.push(...rows);
      renderMatrix();
    }
    $("matrix-status").textContent = TEXT.matrixComplete.replace("{count}", matrixRows.length);
  } catch (error) {
    $("matrix-status").textContent = TEXT.matrixError.replace("{message}", describeError(error));
    recordError(error);
  } finally {
    $("run-matrix-button").disabled = false;
    $("run-all-matrix-button").disabled = false;
    $("run-constraint-probe-button").disabled = false;
    $("run-all-constraint-probe-button").disabled = false;
  }
}

async function runConstraintProbe(runAll) {
  const selectedId = $("camera-select").value;
  let refreshedDevices;
  try {
    refreshedDevices = await refreshDevicesAfterPermission();
  } catch (error) {
    $("constraint-probe-status").textContent = TEXT.constraintError.replace("{message}", describeError(error));
    recordError(error);
    return;
  }
  const targetDevices = selectDiagnosticDevices(refreshedDevices, selectedId, runAll);
  if (targetDevices.length === 0) {
    $("constraint-probe-status").textContent = TEXT.constraintNoInputs;
    return;
  }
  closeWebRtcSender();
  controller.stop();
  $("preview-placeholder").hidden = false;
  constraintProbeRows = [];
  renderConstraintProbe();
  $("run-constraint-probe-button").disabled = true;
  $("run-all-constraint-probe-button").disabled = true;
  $("run-matrix-button").disabled = true;
  $("run-all-matrix-button").disabled = true;
  try {
    for (const { device, index } of targetDevices) {
      const rows = await constraintProbeRunner.runDevice(device, index);
      constraintProbeRows.push(...rows);
      renderConstraintProbe();
    }
    $("constraint-probe-status").textContent = TEXT.constraintComplete.replace("{count}", constraintProbeRows.length);
  } catch (error) {
    $("constraint-probe-status").textContent = TEXT.constraintError.replace("{message}", describeError(error));
    recordError(error);
  } finally {
    $("run-constraint-probe-button").disabled = false;
    $("run-all-constraint-probe-button").disabled = false;
    $("run-matrix-button").disabled = false;
    $("run-all-matrix-button").disabled = false;
  }
}

async function copyConstraintProbe(text, format) {
  try {
    await navigator.clipboard.writeText(text);
    $("constraint-probe-status").textContent = TEXT.clipboardCopied.replace("{format}", format);
  } catch (error) {
    const message = TEXT.clipboardFailed.replace("{message}", describeError(error));
    $("constraint-probe-status").textContent = `● ${message}`;
    recordError(error);
  }
}

$("run-matrix-button").addEventListener("click", () => runMatrix(false));
$("run-all-matrix-button").addEventListener("click", () => runMatrix(true));
$("copy-json-button").addEventListener("click", () => copyMatrix(JSON.stringify(matrixRows, null, 2), "JSON"));
$("copy-csv-button").addEventListener("click", () => copyMatrix(serialiseDiagnosticCSV(matrixRows), "CSV"));
$("run-constraint-probe-button").addEventListener("click", () => runConstraintProbe(false));
$("run-all-constraint-probe-button").addEventListener("click", () => runConstraintProbe(true));
$("copy-constraint-json-button").addEventListener("click", () => copyConstraintProbe(JSON.stringify(constraintProbeRows, null, 2), "JSON"));
$("copy-constraint-csv-button").addEventListener("click", () => copyConstraintProbe(serialiseConstraintProbeCSV(constraintProbeRows), "CSV"));

$("orientation-select").addEventListener("change", () => {
  const settings = readSettings();
  const plan = createOutputPlan(settings);
  video.style.transform = `rotate(${plan.rotationDegrees}deg)`;
  renderRequested(settings);
});

for (const id of ["resolution-select", "frame-rate-select", "quality-select", "camera-select"]) {
  $(id).addEventListener("change", () => {
    latestSettings = readSettings();
    renderRequested(latestSettings);
  });
}
