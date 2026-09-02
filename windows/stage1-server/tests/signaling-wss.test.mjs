import test from "node:test";
import assert from "node:assert/strict";
import { createServer } from "node:http";
import WebSocket from "ws";
import { attachSignalingWebSocket } from "../signaling-wss.mjs";

function waitForMessage(socket) {
  return new Promise((resolve, reject) => {
    const timeout = setTimeout(() => {
      cleanup();
      reject(new Error("timed out waiting for WebSocket message"));
    }, 3000);
    const onMessage = (data) => {
      cleanup();
      resolve(JSON.parse(data.toString()));
    };
    const onError = (error) => {
      cleanup();
      reject(error);
    };
    const cleanup = () => {
      clearTimeout(timeout);
      socket.off("message", onMessage);
      socket.off("error", onError);
    };
    socket.once("message", onMessage);
    socket.once("error", onError);
  });
}

function open(url) {
  return new Promise((resolve, reject) => {
    const socket = new WebSocket(url);
    const timeout = setTimeout(() => {
      socket.close();
      reject(new Error(`timed out opening ${url}`));
    }, 3000);
    socket.once("open", () => {
      clearTimeout(timeout);
      resolve(socket);
    });
    socket.once("error", (error) => {
      clearTimeout(timeout);
      reject(error);
    });
  });
}

async function waitFor(predicate) {
  const deadline = Date.now() + 3000;
  while (!predicate()) {
    if (Date.now() >= deadline) throw new Error("timed out waiting for signaling state");
    await new Promise((resolve) => setTimeout(resolve, 5));
  }
}

async function close(socket) {
  if (socket.readyState === WebSocket.CLOSED) return;
  await new Promise((resolve) => {
    socket.once("close", resolve);
    socket.close();
  });
}

test("accepts same-origin signaling upgrade and relays browser/native messages", async (t) => {
  const httpServer = createServer();
  const events = [];
  const signaling = attachSignalingWebSocket(httpServer, {
    path: "/signaling",
    log: (message) => events.push(message),
  });
  await new Promise((resolve) => httpServer.listen(0, "127.0.0.1", resolve));
  const { port } = httpServer.address();
  const browser = await open(`ws://127.0.0.1:${port}/signaling`);
  const native = await open(`ws://127.0.0.1:${port}/signaling`);
  t.after(async () => {
    await Promise.all([close(browser), close(native)]);
    signaling.close();
    await new Promise((resolve) => httpServer.close(resolve));
  });

  browser.send(JSON.stringify({ type: "hello", role: "browser", sessionId: "s1" }));
  native.send(JSON.stringify({ type: "hello", role: "native", sessionId: "s1" }));
  await waitFor(() => signaling.broker.snapshot().browserConnected && signaling.broker.snapshot().nativeConnected);
  browser.send(JSON.stringify({ type: "offer", sessionId: "s1", sdp: "v=0\r\na=mid:0" }));
  assert.deepEqual(await waitForMessage(native), { type: "offer", sessionId: "s1", sdp: "v=0\r\na=mid:0" });
  native.send(JSON.stringify({ type: "answer", sessionId: "s1", sdp: "v=0\r\na=mid:0" }));
  assert.deepEqual(await waitForMessage(browser), { type: "answer", sessionId: "s1", sdp: "v=0\r\na=mid:0" });
  assert.equal(events.filter((event) => event.includes("WSS connection")).length, 2);
  assert.ok(events.some((event) => event.includes("type=hello role=browser")));
  assert.ok(events.some((event) => event.includes("type=hello role=native")));
  assert.ok(events.some((event) => event.includes("type=offer")));
  assert.ok(events.some((event) => event.includes("type=answer")));
});

test("sends a bounded protocol error for a malformed signaling message", async (t) => {
  const httpServer = createServer();
  const events = [];
  const signaling = attachSignalingWebSocket(httpServer, {
    path: "/signaling",
    maxMessageBytes: 128,
    log: (message) => events.push(message),
  });
  await new Promise((resolve) => httpServer.listen(0, "127.0.0.1", resolve));
  const { port } = httpServer.address();
  const browser = await open(`ws://127.0.0.1:${port}/signaling`);
  t.after(async () => {
    await close(browser);
    signaling.close();
    await new Promise((resolve) => httpServer.close(resolve));
  });

  browser.send("{");
  assert.deepEqual(await waitForMessage(browser), { type: "error", code: "invalid_json" });
  assert.ok(events.some((event) => event.includes("protocol-error") && event.includes("code=invalid_json")));
});
