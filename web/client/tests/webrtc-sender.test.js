import test from "node:test";
import assert from "node:assert/strict";
import { WebRtcSender } from "../src/webrtc-sender.js";

class FakeWebSocket {
  static instances = [];

  constructor(url) {
    this.url = url;
    this.sent = [];
    this.readyState = 0;
    FakeWebSocket.instances.push(this);
  }

  send(message) {
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
  assert.equal(peer.transceiver.codecPreferences.length, 1);
  assert.equal(peer.transceiver.codecPreferences[0].mimeType, "video/H264");

  socket.open();
  await connectPromise;
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
