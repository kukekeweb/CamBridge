import { TEXT } from "./i18n.js";

const TARGET_WIDTH = 1920;
const TARGET_HEIGHT = 1080;
const TARGET_FPS = 60;

function errorMessage(error) {
  return error instanceof Error ? error.message : String(error);
}

function runtimeH264Codecs(senderCapabilities) {
  const capabilities = senderCapabilities ?? globalThis.RTCRtpSender?.getCapabilities?.("video");
  return (capabilities?.codecs ?? []).filter(
    (codec) => typeof codec.mimeType === "string" && codec.mimeType.toLowerCase() === "video/h264",
  );
}

function exactTarget(track) {
  const settings = track?.getSettings?.() ?? {};
  return (
    ((settings.width === TARGET_WIDTH && settings.height === TARGET_HEIGHT) ||
      (settings.width === TARGET_HEIGHT && settings.height === TARGET_WIDTH)) &&
    typeof settings.frameRate === "number" &&
    Math.abs(settings.frameRate - TARGET_FPS) < 0.5
  );
}

export async function configureVideoSender(sender, {
  maxBitrate = 8000000,
  maxFramerate = TARGET_FPS,
} = {}) {
  if (!sender || typeof sender.getParameters !== "function" ||
      typeof sender.setParameters !== "function") {
    return { supported: false, applied: false, reason: "sender parameters API unavailable" };
  }
  const parameters = sender.getParameters() ?? {};
  const encodings = Array.isArray(parameters.encodings) && parameters.encodings.length > 0
    ? parameters.encodings
    : [{}];
  const tuned = {
    ...parameters,
    encodings: encodings.map((encoding) => ({
      ...encoding,
      active: encoding.active !== false,
      maxBitrate,
      maxFramerate,
      scaleResolutionDownBy: 1,
    })),
  };
  // Safari versions that expose the field accept the standard value. If the
  // field is rejected, retry without it so codec negotiation still proceeds.
  tuned.degradationPreference = "maintain-resolution";
  try {
    await sender.setParameters(tuned);
    return { supported: true, applied: true, degradationPreference: true };
  } catch (firstError) {
    const retry = { ...tuned };
    delete retry.degradationPreference;
    try {
      await sender.setParameters(retry);
      return {
        supported: true,
        applied: true,
        degradationPreference: false,
        error: errorMessage(firstError),
      };
    } catch (secondError) {
      return {
        supported: true,
        applied: false,
        error: errorMessage(secondError),
      };
    }
  }
}

function formatTrackSettings(settings) {
  return `${settings?.width ?? "?"}×${settings?.height ?? "?"} / ${settings?.frameRate ?? "?"}fps`;
}

export function formatWebRtcLayout(settings) {
  if (settings?.width === TARGET_WIDTH && settings?.height === TARGET_HEIGHT) return "横向き";
  if (settings?.width === TARGET_HEIGHT && settings?.height === TARGET_WIDTH) return "縦向き";
  return "不明";
}

export function formatWebRtcTrackRequirementError(track, stream) {
  let settings = {};
  try {
    settings = track?.getSettings?.() ?? {};
  } catch {
    settings = {};
  }
  const trackDescription = track
    ? `${formatTrackSettings(settings)}, ${formatWebRtcLayout(settings)}, readyState=${track.readyState ?? "unknown"}`
    : "なし";
  const streamDescription = stream ? "あり" : "なし";
  return `${TEXT.webrtcRequiresExactTrack}（現在のTrack: ${trackDescription}, Stream: ${streamDescription}）`;
}

function iterableStats(report) {
  if (report === null || report === undefined) return [];
  const entries = [];
  if (typeof report.forEach === "function") {
    report.forEach((value, key) => entries.push([key, value]));
    return entries;
  }
  if (typeof report[Symbol.iterator] === "function") {
    for (const entry of report) {
      if (Array.isArray(entry) && entry.length >= 2) entries.push([entry[0], entry[1]]);
    }
    return entries;
  }
  return Object.entries(report);
}

function finiteOrNull(value) {
  return typeof value === "number" && Number.isFinite(value) ? value : null;
}

export function summarizeWebRtcStats(report) {
  const unavailable = {
    available: false,
    codec: null,
    framesPerSecond: null,
    framesEncoded: null,
    framesDropped: null,
    bytesSent: null,
    packetsSent: null,
    packetsLost: null,
    packetLossPercent: null,
    roundTripTimeMs: null,
    jitterMs: null,
    timestamp: null,
  };
  const entries = iterableStats(report);
  const byId = new Map(entries.map(([key, value]) => [value?.id ?? key, value]));
  const outbound = entries
    .map(([, value]) => value)
    .find((value) => value?.type === "outbound-rtp" &&
      (value.kind === "video" || value.mediaType === "video"));
  if (!outbound) return unavailable;

  const remoteInbound = entries
    .map(([, value]) => value)
    .find((value) => value?.type === "remote-inbound-rtp" &&
      (value.kind === "video" || value.mediaType === "video"));
  const codec = outbound.codecId ? byId.get(outbound.codecId) : null;
  const packetsLost = finiteOrNull(remoteInbound?.packetsLost ?? outbound.packetsLost);
  const packetsReceived = finiteOrNull(remoteInbound?.packetsReceived);
  const packetsSent = finiteOrNull(outbound.packetsSent);
  const packetDenominator = packetsLost !== null && packetsReceived !== null
    ? packetsLost + packetsReceived
    : packetsLost !== null && packetsSent !== null
      ? packetsLost + packetsSent
      : null;
  return {
    available: true,
    codec: typeof codec?.mimeType === "string" ? codec.mimeType : null,
    framesPerSecond: finiteOrNull(outbound.framesPerSecond),
    framesEncoded: finiteOrNull(outbound.framesEncoded),
    framesDropped: finiteOrNull(outbound.framesDropped),
    bytesSent: finiteOrNull(outbound.bytesSent),
    packetsSent,
    packetsLost,
    packetLossPercent: packetDenominator && packetDenominator > 0
      ? (packetsLost / packetDenominator) * 100
      : null,
    roundTripTimeMs: finiteOrNull(remoteInbound?.roundTripTime) === null
      ? null
      : remoteInbound.roundTripTime * 1000,
    jitterMs: finiteOrNull(remoteInbound?.jitter) === null
      ? null
      : remoteInbound.jitter * 1000,
    timestamp: finiteOrNull(outbound.timestamp),
  };
}

function defaultSignalingUrl() {
  if (globalThis.location?.protocol !== "https:") {
    throw new Error(TEXT.webrtcRequiresHttps);
  }
  return `wss://${globalThis.location.host}/signaling`;
}

export class WebRtcSender {
  constructor({
    signalingUrl = defaultSignalingUrl(),
    sessionId,
    track,
    stream = null,
    webSocketFactory = (url) => new globalThis.WebSocket(url),
    peerConnectionFactory = (configuration) => new globalThis.RTCPeerConnection(configuration),
    senderCapabilities = undefined,
    onStatus = () => {},
    onStats = () => {},
    statsIntervalMs = 1000,
  } = {}) {
    this.signalingUrl = signalingUrl;
    this.sessionId = sessionId;
    this.track = track;
    this.stream = stream;
    this.webSocketFactory = webSocketFactory;
    this.peerConnectionFactory = peerConnectionFactory;
    this.senderCapabilities = senderCapabilities;
    this.onStatus = onStatus;
    this.onStats = onStats;
    this.statsIntervalMs = statsIntervalMs;
    this.websocket = null;
    this.peerConnection = null;
    this.transceiver = null;
    this.pendingIceCandidates = [];
    this.statsTimer = null;
    this.previousStats = null;
    this.lastFailure = null;
    this.lastSignalingStep = "idle";
    this.state = "idle";
  }

  emitStatus(status) {
    this.state = status;
    this.onStatus(status);
  }

  send(message) {
    if (!this.websocket || this.websocket.readyState !== 1) {
      throw new Error(TEXT.webrtcSocketClosed);
    }
    this.websocket.send(JSON.stringify({ version: 1, ...message }));
  }

  async connect() {
    if (this.state !== "idle" && this.state !== "closed") {
      throw new Error(TEXT.webrtcAlreadyActive);
    }
    if (!this.sessionId) throw new Error(TEXT.webrtcRequiresSession);
    if (!exactTarget(this.track)) {
      const error = formatWebRtcTrackRequirementError(this.track, this.stream);
      this.emitStatus(`error: ${error}`);
      throw new Error(error);
    }

    this.lastFailure = null;
    this.lastSignalingStep = "peer-created";

    const codecs = runtimeH264Codecs(this.senderCapabilities);
    if (codecs.length === 0) {
      throw new Error(TEXT.webrtcH264Unavailable);
    }

    this.emitStatus("connecting");
    this.pendingIceCandidates = [];
    try {
      this.peerConnection = this.peerConnectionFactory({
        iceServers: [],
        iceTransportPolicy: "all",
      });
      this.transceiver = this.peerConnection.addTransceiver(this.track, { direction: "sendonly" });
      if (typeof this.transceiver.setCodecPreferences !== "function") {
        throw new Error(TEXT.webrtcCodecPreferenceUnavailable);
      }
      this.transceiver.setCodecPreferences(codecs);
      this.peerConnection.onicecandidate = (event) => {
        const candidate = event.candidate
          ? (typeof event.candidate.toJSON === "function" ? event.candidate.toJSON() : event.candidate)
          : null;
        const message = { type: "ice", sessionId: this.sessionId, candidate };
        if (!this.websocket || this.websocket.readyState !== 1) {
          this.pendingIceCandidates.push(message);
          return;
        }
        try {
          this.send(message);
        } catch (error) {
          this.fail(error);
        }
      };
      this.peerConnection.onconnectionstatechange = () => {
        const connectionState = this.peerConnection?.connectionState;
        if (connectionState === "connected") this.emitStatus("connected");
        if (["failed", "disconnected", "closed"].includes(connectionState)) {
          const iceState = this.peerConnection?.iceConnectionState ?? "unknown";
          this.emitStatus(`error: WebRTC接続状態が${connectionState}になりました（connectionState=${connectionState}, ICE=${iceState}, signaling step=${this.lastSignalingStep}）`);
        }
      };

      this.websocket = this.webSocketFactory(this.signalingUrl);
      const activeSocket = this.websocket;
      return await new Promise((resolve, reject) => {
        let settled = false;
        const resolveOnce = () => {
          if (!settled) {
            settled = true;
            resolve();
          }
        };
        const rejectOnce = (error) => {
          if (!settled) {
            settled = true;
            reject(error);
          }
        };
        activeSocket.onopen = async () => {
          try {
            this.lastSignalingStep = "socket-open";
            this.send({ type: "hello", role: "browser", sessionId: this.sessionId });
            this.lastSignalingStep = "hello-sent";
            this.lastSignalingStep = "offer-create-begin";
            const offer = await this.peerConnection.createOffer();
            this.lastSignalingStep = "offer-created";
            this.lastSignalingStep = "local-description-begin";
            await this.peerConnection.setLocalDescription(offer);
            this.lastSignalingStep = "local-description-set";
            this.send({
              type: "offer",
              sessionId: this.sessionId,
              sdp: this.peerConnection.localDescription?.sdp ?? offer.sdp,
            });
            this.lastSignalingStep = "offer-sent";
            const pendingIce = this.pendingIceCandidates.splice(0);
            for (const candidate of pendingIce) this.send(candidate);
            this.emitStatus("offered");
            resolveOnce();

            // Sender tuning is deliberately post-offer. Some Safari builds can
            // delay or reject setParameters; it must never block signaling.
            configureVideoSender(this.transceiver.sender)
              .then((result) => { this.senderParameters = result; })
              .catch((error) => {
                this.senderParameters = {
                  supported: true,
                  applied: false,
                  error: errorMessage(error),
                };
              });
          } catch (error) {
            this.fail(error);
            rejectOnce(error);
          }
        };
        activeSocket.onmessage = async (event) => {
          try {
            const message = JSON.parse(event.data);
            if (message.sessionId !== this.sessionId) throw new Error(TEXT.webrtcSessionMismatch);
            if (message.type === "answer") {
              await this.peerConnection.setRemoteDescription({ type: "answer", sdp: message.sdp });
              this.lastSignalingStep = "answer-received";
              this.startStatsPolling();
              return;
            }
            if (message.type === "ice") {
              await this.peerConnection.addIceCandidate(message.candidate ?? null);
              return;
            }
            if (message.type === "close") {
              this.close();
              return;
            }
            if (message.type === "error") {
              throw new Error(TEXT.webrtcSignalingError.replace("{code}", String(message.code ?? "unknown")));
            }
            if (message.type !== "state") throw new Error(TEXT.webrtcUnexpectedMessage);
          } catch (error) {
            this.fail(error);
          }
        };
        activeSocket.onerror = () => {
          const error = new Error(TEXT.webrtcSocketError);
          this.fail(error);
          rejectOnce(error);
        };
        activeSocket.onclose = (event) => {
          if (this.websocket !== activeSocket) return;
          const failure = this.lastFailure;
          this.resetTransport({ closeWebSocket: false });
          if (failure) {
            const closeDetails = `（step=${this.lastSignalingStep}${event?.code ? `, WebSocket close code=${event.code}${event.reason ? `: ${event.reason}` : ""}` : ""}）`;
            this.emitStatus(`error: ${failure}${closeDetails}`);
          } else {
            const closeDetails = `step=${this.lastSignalingStep}${event?.code ? `, WebSocket close code=${event.code}${event.reason ? `: ${event.reason}` : ""}` : ""}`;
            this.state = "closed";
            this.onStatus(`closed: ${closeDetails}`);
          }
        };
      });
    } catch (error) {
      this.fail(error);
      throw error;
    }
  }

  startStatsPolling() {
    if (this.statsTimer !== null || typeof this.peerConnection?.getStats !== "function") return;
    this.collectStats();
    this.statsTimer = globalThis.setInterval(() => {
      this.collectStats();
    }, this.statsIntervalMs);
  }

  async collectStats() {
    const peerConnection = this.peerConnection;
    if (!peerConnection || typeof peerConnection.getStats !== "function") return;
    try {
      const summary = summarizeWebRtcStats(await peerConnection.getStats());
      let bitrateBitsPerSecond = null;
      if (summary.available && this.previousStats?.available &&
          summary.bytesSent !== null && this.previousStats.bytesSent !== null &&
          summary.timestamp !== null && this.previousStats.timestamp !== null &&
          summary.timestamp > this.previousStats.timestamp) {
        bitrateBitsPerSecond = (summary.bytesSent - this.previousStats.bytesSent) * 8 *
          1000 / (summary.timestamp - this.previousStats.timestamp);
      }
      this.previousStats = summary;
      if (this.peerConnection === peerConnection) {
        this.onStats({ ...summary, bitrateBitsPerSecond });
      }
    } catch (error) {
      if (this.peerConnection === peerConnection) {
        this.onStats({ ...summarizeWebRtcStats(null), error: errorMessage(error) });
      }
    }
  }

  fail(error) {
    this.lastFailure = errorMessage(error);
    this.onStatus(`error: ${this.lastFailure}`);
  }

  resetTransport({ closeWebSocket = true } = {}) {
    if (this.statsTimer !== null) {
      globalThis.clearInterval(this.statsTimer);
      this.statsTimer = null;
    }
    this.previousStats = null;
    const peerConnection = this.peerConnection;
    const websocket = this.websocket;
    this.peerConnection = null;
    this.transceiver = null;
    this.websocket = null;
    this.pendingIceCandidates = [];
    peerConnection?.close?.();
    if (closeWebSocket && websocket && websocket.readyState !== 3) {
      websocket.close?.();
    }
  }

  close() {
    this.lastFailure = null;
    this.lastSignalingStep = "closed-by-user";
    this.resetTransport();
    this.emitStatus("closed");
  }
}

export const WEBRTC_TARGET = Object.freeze({
  width: TARGET_WIDTH,
  height: TARGET_HEIGHT,
  frameRate: TARGET_FPS,
});
