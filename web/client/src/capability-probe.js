const CAPABILITY_KEYS = [
  "width",
  "height",
  "frameRate",
  "facingMode",
  "resizeMode",
  "zoom",
  "torch",
  "focusMode",
  "focusDistance",
  "exposureMode",
  "exposureTime",
  "exposureCompensation",
  "whiteBalanceMode",
  "colorTemperature",
];

const SETTINGS_KEYS = [
  "width",
  "height",
  "frameRate",
  "facingMode",
  "deviceId",
  "resizeMode",
  "aspectRatio",
];

function cloneJson(value) {
  if (value === undefined) {
    return undefined;
  }
  if (typeof structuredClone === "function") {
    return structuredClone(value);
  }
  return JSON.parse(JSON.stringify(value));
}

function copyKnownProperties(source, keys) {
  const output = {};
  if (!source || typeof source !== "object") {
    return output;
  }

  for (const key of keys) {
    if (Object.prototype.hasOwnProperty.call(source, key)) {
      output[key] = cloneJson(source[key]);
    }
  }
  return output;
}

export async function enumerateVideoInputs(mediaDevices) {
  const devices = await enumerateVideoInputExposure(mediaDevices);
  return devices.map((device) => {
      const result = { kind: "videoinput" };
      for (const key of ["label", "deviceId", "groupId"]) {
        if (Object.prototype.hasOwnProperty.call(device, key) && device[key]) {
          result[key] = device[key];
        }
      }
      return result;
    });
}

export async function enumerateVideoInputsAfterPermission(mediaDevices) {
  const exposure = await probeVideoDeviceExposure(mediaDevices);
  return exposure.duringActiveCapture;
}

export async function enumerateVideoInputExposure(mediaDevices) {
  const devices = await mediaDevices.enumerateDevices();
  return devices
    .filter((device) => device.kind === "videoinput")
    .map((device) => ({
      kind: "videoinput",
      label: device.label ?? "",
      deviceId: device.deviceId ?? "",
      groupId: device.groupId ?? "",
    }));
}

export function findActiveCaptureDevice(devices, track) {
  const deviceId = track?.getSettings?.()?.deviceId;
  if (!deviceId) return null;
  return devices.find((device) => device.deviceId === deviceId) || null;
}

export function summariseVideoInputExposure(devices) {
  const entries = devices.map((device, index) => ({
    index: index + 1,
    label: device.label,
    labelPresent: Boolean(device.label),
    deviceId: device.deviceId,
    deviceIdPresent: Boolean(device.deviceId),
    groupId: device.groupId,
    groupIdPresent: Boolean(device.groupId),
  }));
  return {
    cameraCount: entries.length,
    deviceIdsPresent: entries.filter((entry) => entry.deviceIdPresent).length,
    labelsPresent: entries.filter((entry) => entry.labelPresent).length,
    groupIdsPresent: entries.filter((entry) => entry.groupIdPresent).length,
    entries,
  };
}

export async function probeVideoDeviceExposure(mediaDevices) {
  const primingStream = await mediaDevices.getUserMedia({ audio: false, video: true });
  let duringActiveCapture;
  try {
    duringActiveCapture = await enumerateVideoInputExposure(mediaDevices);
  } finally {
    primingStream.getTracks?.().forEach((track) => track.stop());
  }
  const afterPrimingTrackStopped = await enumerateVideoInputExposure(mediaDevices);
  return {
    duringActiveCapture,
    afterPrimingTrackStopped,
    duringActiveSummary: summariseVideoInputExposure(duringActiveCapture),
    afterStoppedSummary: summariseVideoInputExposure(afterPrimingTrackStopped),
  };
}

export function snapshotTrackCapabilities(track) {
  return copyKnownProperties(track.getCapabilities(), CAPABILITY_KEYS);
}

export function snapshotTrackSettings(track) {
  return copyKnownProperties(track.getSettings(), SETTINGS_KEYS);
}

export function snapshotTrackConstraints(track) {
  return cloneJson(track.getConstraints());
}

function normaliseCodec(codec) {
  const result = {};
  for (const key of ["mimeType", "clockRate", "channels", "sdpFmtpLine"]) {
    if (Object.prototype.hasOwnProperty.call(codec, key)) {
      result[key] = cloneJson(codec[key]);
    }
  }
  return result;
}

export function probeCodecCapabilities(RTCRtpSenderClass) {
  if (!RTCRtpSenderClass || typeof RTCRtpSenderClass.getCapabilities !== "function") {
    return {
      available: false,
      codecs: [],
      error: "RTCRtpSender.getCapabilities is unavailable",
    };
  }

  try {
    const capabilities = RTCRtpSenderClass.getCapabilities("video");
    return {
      available: true,
      codecs: Array.isArray(capabilities?.codecs)
        ? capabilities.codecs.map(normaliseCodec)
        : [],
      error: null,
    };
  } catch (error) {
    return {
      available: false,
      codecs: [],
      error: error instanceof Error ? error.message : String(error),
    };
  }
}

export function probeLowLatencyAPIs(scope = globalThis) {
  const receiver = scope?.RTCRtpReceiver;
  const prototype = receiver?.prototype;
  return {
    RTCRtpReceiver: Boolean(receiver),
    targetLatency: Boolean(prototype && "targetLatency" in prototype),
    jitterBufferTarget: Boolean(prototype && "jitterBufferTarget" in prototype),
    RTCPeerConnection: typeof scope?.RTCPeerConnection === "function",
    receiverGetStats: typeof prototype?.getStats === "function",
  };
}
