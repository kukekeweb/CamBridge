import { WebSocketServer } from "ws";
import { createSignalingBroker } from "./signaling-session.mjs";

const DEFAULT_PATH = "/signaling";
const DEFAULT_MAX_MESSAGE_BYTES = 512 * 1024;
let endpointCounter = 0;

function sendProtocolError(socket, code) {
  if (socket.readyState === socket.OPEN) {
    socket.send(JSON.stringify({ type: "error", code }));
  }
}

export function attachSignalingWebSocket(server, options = {}) {
  const path = options.path ?? DEFAULT_PATH;
  const maxMessageBytes = options.maxMessageBytes ?? DEFAULT_MAX_MESSAGE_BYTES;
  const broker = options.broker ?? createSignalingBroker({ maxMessageBytes });
  const webSocketServer = new WebSocketServer({ noServer: true, maxPayload: maxMessageBytes });

  const onUpgrade = (request, socket, head) => {
    let pathname;
    try {
      pathname = new URL(request.url ?? "/", "http://cambridge.invalid").pathname;
    } catch {
      socket.destroy();
      return;
    }
    if (pathname !== path) {
      socket.destroy();
      return;
    }
    webSocketServer.handleUpgrade(request, socket, head, (webSocket) => {
      const endpoint = {
        id: `ws-${++endpointCounter}`,
        send(message) {
          if (webSocket.readyState === webSocket.OPEN) webSocket.send(message);
        },
      };
      broker.attach(endpoint);
      webSocket.on("message", (message) => {
        const result = broker.handleMessage(endpoint, message);
        if (!result.ok) sendProtocolError(webSocket, result.code);
      });
      webSocket.once("close", () => broker.detach(endpoint));
      webSocket.once("error", () => broker.detach(endpoint));
    });
  };
  server.on("upgrade", onUpgrade);

  function close() {
    server.off("upgrade", onUpgrade);
    for (const webSocket of webSocketServer.clients) webSocket.close(1001, "server shutdown");
    webSocketServer.close();
  }

  return { broker, webSocketServer, close, path };
}
