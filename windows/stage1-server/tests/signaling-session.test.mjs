import test from "node:test";
import assert from "node:assert/strict";
import { createSignalingBroker } from "../signaling-session.mjs";

function peer(id) {
  const sent = [];
  return {
    id,
    sent,
    send(message) {
      sent.push(JSON.parse(message));
    },
  };
}

function send(broker, endpoint, message) {
  return broker.handleMessage(endpoint, JSON.stringify(message));
}

test("relays hello, offer, answer, and ICE within one browser/native session", () => {
  const broker = createSignalingBroker();
  const browser = peer("browser-1");
  const native = peer("native-1");
  broker.attach(browser);
  broker.attach(native);

  assert.deepEqual(send(broker, browser, { type: "hello", role: "browser", sessionId: "s1" }), { ok: true });
  assert.deepEqual(send(broker, native, { type: "hello", role: "native", sessionId: "s1" }), { ok: true });
  assert.deepEqual(send(broker, browser, { type: "offer", sessionId: "s1", sdp: "v=0\r\na=mid:0" }), { ok: true });
  assert.deepEqual(send(broker, native, { type: "answer", sessionId: "s1", sdp: "v=0\r\na=mid:0" }), { ok: true });
  assert.deepEqual(send(broker, browser, { type: "ice", sessionId: "s1", candidate: { candidate: "candidate:host" } }), { ok: true });

  assert.deepEqual(native.sent.find((message) => message.type === "offer"), { type: "offer", sessionId: "s1", sdp: "v=0\r\na=mid:0" });
  assert.deepEqual(native.sent.at(-1), { type: "ice", sessionId: "s1", candidate: { candidate: "candidate:host" } });
  assert.deepEqual(browser.sent.at(-1), { type: "answer", sessionId: "s1", sdp: "v=0\r\na=mid:0" });
});

test("rejects messages before hello and does not occupy the session", () => {
  const broker = createSignalingBroker();
  const browser = peer("browser-1");
  const native = peer("native-1");
  broker.attach(browser);

  assert.deepEqual(send(broker, browser, { type: "offer", sessionId: "s1", sdp: "v=0" }), {
    ok: false,
    code: "hello_required",
  });
  assert.deepEqual(send(broker, browser, { type: "hello", role: "browser", sessionId: "s1" }), { ok: true });
  broker.attach(native);
  assert.deepEqual(send(broker, native, { type: "hello", role: "native", sessionId: "s2" }), {
    ok: false,
    code: "session_mismatch",
  });
});

test("rejects a second browser or native endpoint and bounds malformed messages", () => {
  const broker = createSignalingBroker({ maxMessageBytes: 64 });
  const browser = peer("browser-1");
  const browser2 = peer("browser-2");
  broker.attach(browser);
  broker.attach(browser2);
  assert.deepEqual(send(broker, browser, { type: "hello", role: "browser", sessionId: "s1" }), { ok: true });
  assert.deepEqual(send(broker, browser2, { type: "hello", role: "browser", sessionId: "s1" }), {
    ok: false,
    code: "role_in_use",
  });
  assert.deepEqual(broker.handleMessage(browser, "{"), { ok: false, code: "invalid_json" });
  assert.deepEqual(broker.handleMessage(browser, "x".repeat(65)), { ok: false, code: "message_too_large" });
});

test("allows a fresh session after both endpoints detach", () => {
  const broker = createSignalingBroker();
  const browser = peer("browser-1");
  const native = peer("native-1");
  broker.attach(browser);
  broker.attach(native);
  send(broker, browser, { type: "hello", role: "browser", sessionId: "s1" });
  send(broker, native, { type: "hello", role: "native", sessionId: "s1" });
  broker.detach(browser);
  broker.detach(native);

  const browser2 = peer("browser-2");
  broker.attach(browser2);
  assert.deepEqual(send(broker, browser2, { type: "hello", role: "browser", sessionId: "s2" }), { ok: true });
});
