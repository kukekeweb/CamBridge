import test from "node:test";
import assert from "node:assert/strict";
import {
  WebRtcSender,
  configureVideoSender,
  formatWebRtcLayout,
  formatWebRtcTrackRequirementError,
  summarizeWebRtcStats,
} from "../src/webrtc-sender.js";

class FakeWebSocket {
  static instances = [];

  constructor(url) {
    this.url = url;
    this.sent = [];
    this.readyState = 0;
    FakeWebSocket.instances.push(this);
  }

  send(message) {
    if (this.readyState !== 1) throw new Error("WebSocket is not open");
    this.sent.push(JSON.parse(message));
  }

  close() {
    this.readyState = 3;
    this.onclose?.();
  }

  open() {
    this.readyState = 1;
    this.onopen?.();
  }

  receive(message) {
    this.onmessage?.({ data: JSON.stringify(message) });
  }
}

class FakePeerConnection {
  static instances = [];
  static senderSetParameters = null;

  constructor(configuration) {
    this.configuration = configuration;
    this.transceiver = null;
    this.localDescription = null;
    this.remoteDescription = null;
    this.addedCandidates = [];
    FakePeerConnection.instances.push(this);
  }

  addTransceiver(track, options) {
    this.transceiver = {
      track,
      options,
      sender: {
        parameters: { encodings: [{}] },
        getParameters() { return this.parameters; },
        async setParameters(parameters) {
          if (FakePeerConnection.senderSetParameters) {
            return FakePeerConnection.senderSetParameters.call(this, parameters);
          }
          this.parameters = parameters;
        },
      },
      codecPreferences: null,
      setCodecPreferences: (codecs) => {
        this.transceiver.codecPreferences = codecs;
      },
    };
    return this.transceiver;
  }

  async createOffer() {
    return { type: "offer", sdp: "v=0\r\nm=video 9 UDP/TLS/RTP/SAVPF 96" };
  }

  async setLocalDescription(description) {
    this.localDescription = description;
  }

  async setRemoteDescription(description) {
    this.remoteDescription = description;
    this.connectionState = "connected";
    this.onconnectionstatechange?.();
  }

  async addIceCandidate(candidate) {
    this.addedCandidates.push(candidate);
  }

  close() {
    this.connectionState = "closed";
  }
}

const track = {
  readyState: "live",
  getSettings() {
    return { width: 1920, height: 1080, frameRate: 60 };
  },
};

const stream = { id: "stream-1" };
const capabilities = {
  codecs: [
    { mimeType: "video/VP8" },
    { mimeType: "video/H264", sdpFmtpLine: "profile-level-id=42e01f" },
  ],
};

function makeSender(overrides = {}) {
  FakeWebSocket.instances = [];
  FakePeerConnection.instances = [];
  FakePeerConnection.senderSetParameters = null;
  return new WebRtcSender({
    signalingUrl: "wss://192.168.11.2:8443/signaling",
    sessionId: "s1",
    track,
    stream,
    webSocketFactory: (url) => new FakeWebSocket(url),
    peerConnectionFactory: (configuration) => new FakePeerConnection(configuration),
    senderCapabilities: capabilities,
    ...overrides,
  });
}

test("creates a LAN-only H264 offer and handles answer/ICE", async () => {
  const statuses = [];
  const sender = makeSender({ onStatus: (status) => statuses.push(status) });
  const connectPromise = sender.connect();
  const socket = FakeWebSocket.instances[0];
  const peer = FakePeerConnection.instances[0];
  assert.equal(peer.configuration.iceServers.length, 0);
  assert.deepEqual(peer.transceiver.options, { direction: "sendonly" });
  assert.equal(peer.transceiver.codecPreferences, null);

  socket.open();
  assert.equal(peer.transceiver.codecPreferences.length, 1);
  assert.equal(peer.transceiver.codecPreferences[0].mimeType, "video/H264");
  await connectPromise;
  assert.equal(peer.transceiver.sender.parameters.encodings[0].scaleResolutionDownBy, 1);
  assert.equal(peer.transceiver.sender.parameters.encodings[0].maxFramerate, 60);
  assert.equal(peer.transceiver.sender.parameters.degradationPreference, "maintain-resolution");
  assert.deepEqual(socket.sent[0], { version: 1, type: "hello", role: "browser", sessionId: "s1" });
  assert.equal(socket.sent[1].type, "offer");
  assert.equal(socket.sent[1].sessionId, "s1");

  peer.onicecandidate({ candidate: { candidate: "candidate:1", sdpMid: "0" } });
  assert.equal(socket.sent[2].type, "ice");
  assert.equal(socket.sent[2].candidate.candidate, "candidate:1");
  socket.receive({ version: 1, type: "answer", sessionId: "s1", sdp: "v=0\r\nm=video" });
  socket.receive({ version: 1, type: "ice", sessionId: "s1", candidate: { candidate: "candidate:2", sdpMid: "0" } });
  await new Promise((resolve) => setImmediate(resolve));
  assert.equal(peer.remoteDescription.type, "answer");
  assert.equal(peer.addedCandidates.length, 1);
  assert.ok(statuses.includes("connected"));
  sender.close();
});

test("does not block Offer on Safari sender parameter tuning", async () => {
  const sender = makeSender();
  FakePeerConnection.senderSetParameters = () => new Promise(() => {});
  const connectPromise = sender.connect();
  const socket = FakeWebSocket.instances[0];
  socket.open();
  await connectPromise;
  assert.equal(socket.sent[0].type, "hello");
  assert.equal(socket.sent[1].type, "offer");
  sender.close();
  FakePeerConnection.senderSetParameters = null;
});

test("cleans up the signaling socket when Offer creation fails", async () => {
  const sender = makeSender();
  const connectPromise = sender.connect();
  const peer = FakePeerConnection.instances[0];
  const socket = FakeWebSocket.instances[0];
  peer.createOffer = () => Promise.reject(new Error("Offer作成テストエラー"));
  socket.open();
  await assert.rejects(connectPromise, /Offer作成テストエラー/);
  assert.equal(sender.websocket, null);
  assert.equal(sender.peerConnection, null);
  assert.equal(socket.readyState, 3);
});

test("reports codec preference failures after the signaling hello boundary", async () => {
  const statuses = [];
  const sender = makeSender({
    onStatus: (status) => statuses.push(status),
  });
  const originalFactory = FakePeerConnection.prototype.addTransceiver;
  FakePeerConnection.prototype.addTransceiver = function (...args) {
    const transceiver = originalFactory.call(this, ...args);
    transceiver.setCodecPreferences = () => {
      throw new Error("codec preference test failure");
    };
    return transceiver;
  };
  try {
    const connectPromise = sender.connect();
    const socket = FakeWebSocket.instances[0];
    socket.open();
    await assert.rejects(connectPromise, /codec preference test failure/);
    assert.deepEqual(socket.sent[0], { version: 1, type: "hello", role: "browser", sessionId: "s1" });
    assert.equal(socket.sent.length, 1);
    assert.ok(statuses.includes("connecting:codec-preference"));
  } finally {
    FakePeerConnection.prototype.addTransceiver = originalFactory;
  }
});

test("configures the sender without failing when degradationPreference is unsupported", async () => {
  let calls = 0;
  const sender = {
    getParameters: () => ({ encodings: [{}] }),
    setParameters: async (parameters) => {
      calls += 1;
      if (parameters.degradationPreference) throw new Error("unsupported field");
      sender.parameters = parameters;
    },
  };
  const result = await configureVideoSender(sender);
  assert.equal(result.applied, true);
  assert.equal(result.degradationPreference, false);
  assert.equal(calls, 2);
  assert.equal(sender.parameters.encodings[0].scaleResolutionDownBy, 1);
  assert.equal(sender.parameters.encodings[0].maxFramerate, 60);
  assert.equal(sender.parameters.encodings[0].maxBitrate, 8000000);
});

test("fails without runtime H264 capability and does not silently choose another codec", async () => {
  const sender = makeSender({ senderCapabilities: { codecs: [{ mimeType: "video/VP8" }] } });
  await assert.rejects(sender.connect(), /H\.264/);
  assert.equal(FakeWebSocket.instances.length, 0);
});

test("rejects a capture track that is not the exact Stage 2 target", async () => {
  const sender = makeSender({
    track: { readyState: "live", getSettings: () => ({ width: 1920, height: 1080, frameRate: 30 }) },
  });
  await assert.rejects(sender.connect(), /1920×1080.*60/);
});

test("describes the current track when WebRTC cannot start", () => {
  assert.match(
    formatWebRtcTrackRequirementError({
      readyState: "live",
      getSettings: () => ({ width: 1920, height: 1080, frameRate: 60 }),
    }, null),
    /現在のTrack: 1920×1080 \/ 60fps, 横向き, readyState=live, Stream: なし/,
  );
});

test("reports the exact-track precondition failure through the status callback", async () => {
  const statuses = [];
  const sender = makeSender({
    track: { readyState: "live", getSettings: () => ({ width: 1920, height: 1080, frameRate: 30 }) },
    onStatus: (status) => statuses.push(status),
  });
  await assert.rejects(sender.connect(), /1920×1080.*60/);
  assert.match(statuses.at(-1), /^error: .*現在のTrack: 1920×1080 \/ 30fps/);
});

test("accepts a portrait 1080x1920 track at 60fps", async () => {
  const sender = makeSender({
    track: { readyState: "live", getSettings: () => ({ width: 1080, height: 1920, frameRate: 60 }) },
  });
  const connectPromise = sender.connect();
  const socket = FakeWebSocket.instances[0];
  socket.open();
  await connectPromise;
  assert.equal(formatWebRtcLayout({ width: 1080, height: 1920 }), "縦向き");
  assert.equal(socket.sent[1].type, "offer");
  sender.close();
});

test("keeps unsupported dimensions rejected", async () => {
  const sender = makeSender({
    track: { readyState: "live", getSettings: () => ({ width: 1280, height: 720, frameRate: 60 }) },
  });
  await assert.rejects(sender.connect(), /1920×1080.*60/);
  assert.equal(formatWebRtcLayout({ width: 1280, height: 720 }), "不明");
});

test("queues ICE candidates emitted before the signaling socket opens", async () => {
  const sender = makeSender();
  const connectPromise = sender.connect();
  const socket = FakeWebSocket.instances[0];
  const peer = FakePeerConnection.instances[0];

  peer.onicecandidate({ candidate: { candidate: "candidate:early", sdpMid: "0" } });
  assert.equal(socket.sent.length, 0);

  socket.open();
  await connectPromise;
  assert.equal(socket.sent[0].type, "hello");
  assert.equal(socket.sent[1].type, "offer");
  assert.equal(socket.sent[2].type, "ice");
  assert.equal(socket.sent[2].candidate.candidate, "candidate:early");
  sender.close();
});

test("cleans up an unexpected signaling close so the sender can reconnect", async () => {
  const statuses = [];
  const sender = makeSender({ onStatus: (status) => statuses.push(status) });
  const firstConnect = sender.connect();
  const firstSocket = FakeWebSocket.instances[0];
  const firstPeer = FakePeerConnection.instances[0];
  firstSocket.open();
  await firstConnect;
  firstSocket.receive({ version: 1, type: "answer", sessionId: "s1", sdp: "v=0\r\nm=video" });
  await new Promise((resolve) => setImmediate(resolve));

  firstSocket.readyState = 3;
  firstSocket.onclose?.();

  assert.equal(firstPeer.connectionState, "closed");
  assert.equal(sender.peerConnection, null);
  assert.equal(sender.websocket, null);
  assert.match(statuses.at(-1), /^closed: /);
  assert.match(statuses.at(-1), /step=answer-received/);

  const secondConnect = sender.connect();
  const secondSocket = FakeWebSocket.instances[1];
  secondSocket.open();
  await secondConnect;
  assert.notEqual(FakePeerConnection.instances[1], firstPeer);
  sender.close();
});

test("keeps the signaling failure visible when the WebSocket closes afterward", async () => {
  const statuses = [];
  const sender = makeSender({ onStatus: (status) => statuses.push(status) });
  const connectPromise = sender.connect();
  const socket = FakeWebSocket.instances[0];
  socket.open();
  await connectPromise;

  socket.receive({ version: 1, type: "error", sessionId: "s1", code: "peer_not_connected" });
  await new Promise((resolve) => setImmediate(resolve));
  socket.readyState = 3;
  socket.onclose?.({ code: 1006, reason: "abnormal closure" });

  assert.match(statuses.at(-1), /^error: /);
  assert.match(statuses.at(-1), /signalingでエラーが通知されました/);
  assert.match(statuses.at(-1), /code=1006/);
});

test("reports an abnormal WebSocket close even without a prior signaling error", async () => {
  const statuses = [];
  const sender = makeSender({ onStatus: (status) => statuses.push(status) });
  const connectPromise = sender.connect();
  const socket = FakeWebSocket.instances[0];
  socket.open();
  await connectPromise;

  socket.readyState = 3;
  socket.onclose?.({ code: 1006, reason: "connection reset", wasClean: false });

  assert.match(statuses.at(-1), /^closed: /);
  assert.match(statuses.at(-1), /WebSocket close code=1006/);
  assert.match(statuses.at(-1), /connection reset/);
});

test("reports peer connection failure state with the signaling stage", async () => {
  const statuses = [];
  const sender = makeSender({ onStatus: (status) => statuses.push(status) });
  const connectPromise = sender.connect();
  const socket = FakeWebSocket.instances[0];
  const peer = FakePeerConnection.instances[0];
  socket.open();
  await connectPromise;

  peer.connectionState = "failed";
  peer.onconnectionstatechange?.();

  assert.match(statuses.at(-1), /^error: /);
  assert.match(statuses.at(-1), /connectionState=failed/);
  assert.match(statuses.at(-1), /signaling step=offer-sent/);
  sender.close();
});

test("summarizes outbound video stats and the negotiated codec", () => {
  const report = new Map([
    ["outbound-1", {
      id: "outbound-1",
      type: "outbound-rtp",
      kind: "video",
      codecId: "codec-1",
      framesPerSecond: 59.8,
      framesEncoded: 120,
      bytesSent: 2_000_000,
      packetsSent: 240,
      framesDropped: 2,
      timestamp: 10_000,
    }],
    ["codec-1", { id: "codec-1", type: "codec", mimeType: "video/H264" }],
    ["remote-1", {
      id: "remote-1",
      type: "remote-inbound-rtp",
      kind: "video",
      packetsLost: 3,
      packetsReceived: 237,
      roundTripTime: 0.012,
      jitter: 0.001,
    }],
  ]);

  assert.deepEqual(summarizeWebRtcStats(report), {
    available: true,
    codec: "video/H264",
    framesPerSecond: 59.8,
    framesEncoded: 120,
    framesDropped: 2,
    bytesSent: 2_000_000,
    packetsSent: 240,
    packetsLost: 3,
    packetLossPercent: 1.25,
    roundTripTimeMs: 12,
    jitterMs: 1,
    timestamp: 10_000,
  });
});

test("reports unavailable when the runtime does not expose getStats data", () => {
  assert.deepEqual(summarizeWebRtcStats(null), {
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
  });
});
