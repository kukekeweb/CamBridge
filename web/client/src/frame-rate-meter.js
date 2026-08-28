export function rateForSamples(samples, windowSeconds) {
  if (!Array.isArray(samples) || samples.length < 2 || windowSeconds <= 0) {
    return 0;
  }

  const latest = samples.at(-1);
  const cutoff = latest - windowSeconds * 1000;
  const windowSamples = samples.filter((timestamp) => timestamp >= cutoff);
  if (windowSamples.length < 2) {
    return 0;
  }

  const elapsedMilliseconds = windowSamples.at(-1) - windowSamples[0];
  if (elapsedMilliseconds <= 0) {
    return 0;
  }
  return (windowSamples.length - 1) / (elapsedMilliseconds / 1000);
}

export function estimateMissingFrames(frameCount, elapsedSeconds, targetFPS) {
  if (frameCount < 0 || elapsedSeconds <= 0 || targetFPS <= 0) {
    return 0;
  }
  return Math.max(0, Math.round(elapsedSeconds * targetFPS) - frameCount);
}

export class FrameRateMeter {
  constructor() {
    this.supported = false;
    this._running = false;
    this._video = null;
    this._requestId = null;
    this._samples = [];
    this._frameCount = 0;
    this._targetFPS = 0;
    this._onUpdate = () => {};
  }

  start(video, targetFPS, onUpdate = () => {}) {
    this.stop();
    this._video = video;
    this._targetFPS = targetFPS;
    this._onUpdate = onUpdate;
    this._samples = [];
    this._frameCount = 0;
    this.supported = typeof video?.requestVideoFrameCallback === "function";

    if (!this.supported) {
      this._onUpdate({ available: false });
      return;
    }

    this._running = true;
    const onFrame = (timestamp, metadata = {}) => {
      if (!this._running) {
        return;
      }

      this._samples.push(timestamp);
      this._samples = this._samples.filter(
        (sample) => sample >= timestamp - 10_000,
      );
      this._frameCount += 1;
      const elapsedSeconds = Math.max(0, (timestamp - this._samples[0]) / 1000);
      const update = {
        available: true,
        oneSecondFPS: rateForSamples(this._samples, 1),
        tenSecondFPS: rateForSamples(this._samples, 10),
        frameCount: this._frameCount,
        missingFrames: estimateMissingFrames(
          this._frameCount,
          elapsedSeconds,
          this._targetFPS,
        ),
      };
      if (typeof metadata.presentedFrames === "number") {
        update.presentedFrames = metadata.presentedFrames;
      }
      this._onUpdate(update);
      this._requestId = this._video.requestVideoFrameCallback(onFrame);
    };

    this._requestId = video.requestVideoFrameCallback(onFrame);
  }

  stop() {
    this._running = false;
    if (
      this._video &&
      this._requestId !== null &&
      typeof this._video.cancelVideoFrameCallback === "function"
    ) {
      this._video.cancelVideoFrameCallback(this._requestId);
    }
    this._requestId = null;
    this._video = null;
  }
}
