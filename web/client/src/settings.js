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

export function buildExactVideoConstraints(settings, deviceId = settings.cameraId) {
  const constraints = {
    audio: false,
    video: {
      width: { exact: settings.resolution.width },
      height: { exact: settings.resolution.height },
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
  const portrait = settings.orientation === "portrait";
  const outputDimensions = portrait
    ? { width: requestedDimensions.height, height: requestedDimensions.width }
    : { ...requestedDimensions };

  return {
    requestedDimensions,
    requestedFPS: settings.frameRate,
    outputDimensions,
    rotationDegrees: portrait ? 90 : 0,
    orientation: settings.orientation,
    transportTransform: "future-stage-2",
    fallback: false,
  };
}
