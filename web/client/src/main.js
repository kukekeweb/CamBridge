import {
  enumerateVideoInputs,
  probeCodecCapabilities,
  probeLowLatencyAPIs,
} from "./capability-probe.js";
import { CaptureController } from "./capture-controller.js";
import { FrameRateMeter } from "./frame-rate-meter.js";
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

function json(value) {
  return JSON.stringify(value, null, 2);
}

function recordError(error) {
  const message = error instanceof Error ? error.message : String(error);
  errors.push(message);
  $("errors-output").textContent = json(errors);
}

function renderRequested(settings) {
  $("requested-value").textContent = `${settings.resolution.width}×${settings.resolution.height} @ ${settings.frameRate}`;
  const plan = createOutputPlan(settings);
  $("output-plan-value").textContent = `${plan.outputDimensions.width}×${plan.outputDimensions.height}, ${plan.rotationDegrees}° (${plan.transportTransform})`;
}

function populateCameraSelect() {
  const select = $("camera-select");
  const previous = select.value;
  select.replaceChildren();
  if (devices.length === 0) {
    select.append(new Option("No video inputs reported", ""));
    return;
  }
  devices.forEach((device, index) => {
    const label = device.label || `Camera ${index + 1} (label unavailable)`;
    select.append(new Option(label, device.deviceId || ""));
  });
  if ([...select.options].some((option) => option.value === previous)) {
    select.value = previous;
  }
}

async function refreshDevices() {
  if (!navigator.mediaDevices?.enumerateDevices) {
    recordError("navigator.mediaDevices.enumerateDevices is unavailable");
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
    item.textContent = result.error || "Codec capability API unavailable";
    list.append(item);
    return;
  }
  if (result.codecs.length === 0) {
    const item = document.createElement("li");
    item.textContent = "No video codecs returned by Safari";
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
    $("measured-value").textContent = "Unavailable (requestVideoFrameCallback missing)";
    $("frame-counter-value").textContent = "—";
    $("missing-value").textContent = "—";
    return;
  }
  $("measured-value").textContent = `${update.oneSecondFPS.toFixed(2)} fps (10 s: ${update.tenSecondFPS.toFixed(2)} fps)`;
  $("frame-counter-value").textContent = String(update.frameCount);
  $("missing-value").textContent = String(update.missingFrames);
}

const meter = new FrameRateMeter();
const controller = new CaptureController({
  mediaDevices: navigator.mediaDevices,
  video,
  meter,
  onMeterUpdate: renderMeter,
});

$("latency-output").textContent = json(probeLowLatencyAPIs(globalThis));
renderCodecs();
renderRequested(latestSettings);
refreshDevices();

$("start-button").addEventListener("click", async () => {
  const settings = readSettings();
  latestSettings = settings;
  renderRequested(settings);
  $("capture-status").textContent = "● Starting exact request…";
  $("capture-status").className = "status";
  $("start-button").disabled = true;
  try {
    const result = await controller.start(settings, settings.cameraId);
    $("settings-output").textContent = json(result.actualSettings ?? {});
    $("capabilities-output").textContent = json(result.capabilities ?? {});
    $("constraints-output").textContent = json(result.constraints ?? {});
    $("actual-value").textContent = result.actualSettings
      ? `${result.actualSettings.width ?? "?"}×${result.actualSettings.height ?? "?"} @ ${result.actualSettings.frameRate ?? "?"}`
      : "—";
    if (result.ok) {
      $("capture-status").textContent = "● Running";
      $("capture-status").className = "status running";
      $("preview-placeholder").hidden = true;
      await refreshDevices();
    } else {
      $("capture-status").textContent = `● ${result.message}`;
      $("capture-status").className = "status error";
      recordError(result.message);
    }
  } catch (error) {
    $("capture-status").textContent = `● ${error instanceof Error ? error.message : String(error)}`;
    $("capture-status").className = "status error";
    recordError(error);
  } finally {
    $("start-button").disabled = false;
  }
});

$("stop-button").addEventListener("click", () => {
  controller.stop();
  $("preview-placeholder").hidden = false;
  $("capture-status").textContent = "● Idle";
  $("capture-status").className = "status";
  $("actual-value").textContent = "—";
});

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
