export const RESOLUTIONS = Object.freeze([
  Object.freeze({ width: 1920, height: 1080, label: "1920×1080" }),
  Object.freeze({ width: 2560, height: 1440, label: "2560×1440" }),
  Object.freeze({ width: 3840, height: 2160, label: "3840×2160" }),
]);

export const FRAME_RATES = Object.freeze([30, 60]);
export const ORIENTATIONS = Object.freeze(["auto", "portrait", "landscape"]);
export const QUALITY_PRESETS = Object.freeze(["low", "medium", "high"]);

const DEFAULT_SETTINGS = Object.freeze({
  cameraId: null,
  orientation: "auto",
  resolution: RESOLUTIONS[0],
  frameRate: 60,
  quality: "high",
});

function copyResolution(resolution) {
  return {
    width: Number(resolution.width),
    height: Number(resolution.height),
    ...(resolution.label ? { label: resolution.label } : {}),
  };
}

export function createSettings(overrides = {}) {
  const resolution = overrides.resolution ?? DEFAULT_SETTINGS.resolution;
  return {
    ...DEFAULT_SETTINGS,
    ...overrides,
    resolution: copyResolution(resolution),
  };
}

export function resolveCaptureOrientation(settings, viewport = {}) {
  if (settings?.orientation === "portrait" || settings?.orientation === "landscape") {
    return settings.orientation;
  }

  const screenOrientationType = String(viewport.screenOrientationType ?? "").toLowerCase();
  if (screenOrientationType.includes("portrait")) {
    return "portrait";
  }
  if (screenOrientationType.includes("landscape")) {
    return "landscape";
  }

  const width = Number(viewport.width);
  const height = Number(viewport.height);
  return width > 0 && height > width ? "portrait" : "landscape";
}

export function captureDimensions(settings, orientationOverride = undefined) {
  const resolution = settings?.resolution ?? RESOLUTIONS[0];
  const orientation = orientationOverride ?? resolveCaptureOrientation(settings);
  if (orientation === "portrait") {
    return { width: Number(resolution.height), height: Number(resolution.width) };
  }
  return { width: Number(resolution.width), height: Number(resolution.height) };
}

export function buildExactVideoConstraints(
  settings,
  deviceId = settings.cameraId,
  orientationOverride = undefined,
) {
  const dimensions = captureDimensions(settings, orientationOverride);
  const constraints = {
    audio: false,
    video: {
      width: { exact: dimensions.width },
      height: { exact: dimensions.height },
      frameRate: { exact: settings.frameRate },
    },
  };

  if (deviceId) {
    constraints.video.deviceId = { exact: deviceId };
  }

  return constraints;
}

export function createOutputPlan(settings) {
  const requestedDimensions = {
    width: settings.resolution.width,
    height: settings.resolution.height,
  };
  const orientation = resolveCaptureOrientation(settings);
  const portrait = orientation === "portrait";
  const outputDimensions = captureDimensions(settings, orientation);

  return {
    requestedDimensions,
    requestedFPS: settings.frameRate,
    outputDimensions,
    // The capture track already carries the selected orientation in its dimensions.
    // Do not rotate it again in the preview or transport layer.
    rotationDegrees: 0,
    orientation,
    transportTransform: "identity",
    fallback: false,
  };
}
