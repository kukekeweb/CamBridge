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
    settings.width === TARGET_WIDTH &&
    settings.height === TARGET_HEIGHT &&
    typeof settings.frameRate === "number" &&
    Math.abs(settings.frameRate - TARGET_FPS) < 0.5
  );
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
  } = {}) {
    this.signalingUrl = signalingUrl;
    this.sessionId = sessionId;
    this.track = track;
    this.stream = stream;
    this.webSocketFactory = webSocketFactory;
    this.peerConnectionFactory = peerConnectionFactory;
    this.senderCapabilities = senderCapabilities;
    this.onStatus = onStatus;
    this.websocket = null;
    this.peerConnection = null;
    this.transceiver = null;
    this.pendingIceCandidates = [];
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
      throw new Error(TEXT.webrtcRequiresExactTrack);
    }

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
          this.emitStatus(connectionState);
        }
      };

      this.websocket = this.webSocketFactory(this.signalingUrl);
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
        this.websocket.onopen = async () => {
          try {
            this.send({ type: "hello", role: "browser", sessionId: this.sessionId });
            const offer = await this.peerConnection.createOffer();
            await this.peerConnection.setLocalDescription(offer);
            this.send({
              type: "offer",
              sessionId: this.sessionId,
              sdp: this.peerConnection.localDescription?.sdp ?? offer.sdp,
            });
            const pendingIce = this.pendingIceCandidates.splice(0);
            for (const candidate of pendingIce) this.send(candidate);
            this.emitStatus("offered");
            resolveOnce();
          } catch (error) {
            this.fail(error);
            rejectOnce(error);
          }
        };
        this.websocket.onmessage = async (event) => {
          try {
            const message = JSON.parse(event.data);
            if (message.sessionId !== this.sessionId) throw new Error(TEXT.webrtcSessionMismatch);
            if (message.type === "answer") {
              await this.peerConnection.setRemoteDescription({ type: "answer", sdp: message.sdp });
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
        this.websocket.onerror = () => {
          const error = new Error(TEXT.webrtcSocketError);
          this.fail(error);
          rejectOnce(error);
        };
        this.websocket.onclose = () => {
          if (this.state !== "closed") this.emitStatus("closed");
        };
      });
    } catch (error) {
      this.fail(error);
      throw error;
    }
  }

  fail(error) {
    this.onStatus(`error: ${errorMessage(error)}`);
  }

  close() {
    this.peerConnection?.close?.();
    this.websocket?.close?.();
    this.peerConnection = null;
    this.transceiver = null;
    this.websocket = null;
    this.pendingIceCandidates = [];
    this.emitStatus("closed");
  }
}

export const WEBRTC_TARGET = Object.freeze({
  width: TARGET_WIDTH,
  height: TARGET_HEIGHT,
  frameRate: TARGET_FPS,
});
