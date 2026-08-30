const DEFAULT_MAX_MESSAGE_BYTES = 64 * 1024;
const MAX_SESSION_ID_LENGTH = 128;
const MAX_SDP_LENGTH = 512 * 1024;
const AUTO_SESSION_ID = "auto";
const ROLES = new Set(["browser", "native"]);
const RELAY_TYPES = new Set(["offer", "answer", "ice", "close"]);

function resultOk() {
  return { ok: true };
}

function resultError(code) {
  return { ok: false, code };
}

function isValidSessionId(value) {
  return typeof value === "string" && value.length > 0 && value.length <= MAX_SESSION_ID_LENGTH;
}

function isValidSdp(value) {
  return typeof value === "string" && value.length > 0 && value.length <= MAX_SDP_LENGTH;
}

function isValidIceCandidate(value) {
  return value === null || (typeof value === "object" && !Array.isArray(value));
}

function peerKey(endpoint) {
  return endpoint?.id;
}

export function createSignalingBroker(options = {}) {
  const maxMessageBytes = options.maxMessageBytes ?? DEFAULT_MAX_MESSAGE_BYTES;
  if (!Number.isInteger(maxMessageBytes) || maxMessageBytes < 1) {
    throw new RangeError("maxMessageBytes must be a positive integer");
  }

  const endpoints = new Map();
  let sessionId = null;
  let browser = null;
  let native = null;
  let nativeAuto = false;

  function attach(endpoint) {
    if (!endpoint || typeof endpoint.send !== "function" || peerKey(endpoint) === undefined) {
      throw new TypeError("endpoint must have an id and send(message) function");
    }
    endpoints.set(peerKey(endpoint), { endpoint, role: null, sessionId: null });
  }

  function detach(endpoint) {
    const key = peerKey(endpoint);
    const record = endpoints.get(key);
    if (!record || record.endpoint !== endpoint) return;
    if (record.role === "browser" && browser?.endpoint === endpoint) browser = null;
    if (record.role === "native" && native?.endpoint === endpoint) {
      native = null;
      nativeAuto = false;
    }
    if (record.role === "browser" && nativeAuto && native !== null) {
      sessionId = null;
      native.sessionId = AUTO_SESSION_ID;
    }
    endpoints.delete(key);
    if (browser === null && native === null) sessionId = null;
  }

  function setRole(record, role, requestedSessionId) {
    if (!ROLES.has(role) || !isValidSessionId(requestedSessionId)) {
      return resultError("invalid_hello");
    }
    const autoNative = role === "native" && requestedSessionId === AUTO_SESSION_ID;
    if (requestedSessionId === AUTO_SESSION_ID && !autoNative) {
      return resultError("invalid_hello");
    }
    if (record.role !== null && (record.role !== role || record.sessionId !== requestedSessionId)) {
      return resultError("hello_already_bound");
    }
    const effectiveSessionId = autoNative ? (sessionId ?? AUTO_SESSION_ID) : requestedSessionId;
    if (sessionId !== null && sessionId !== effectiveSessionId) {
      return resultError("session_mismatch");
    }
    const current = role === "browser" ? browser : native;
    if (current !== null && current.endpoint !== record.endpoint) {
      return resultError("role_in_use");
    }
    if (!autoNative) sessionId = requestedSessionId;
    record.role = role;
    record.sessionId = effectiveSessionId;
    if (role === "browser") browser = record;
    else {
      native = record;
      nativeAuto = autoNative;
      if (sessionId !== null) record.sessionId = sessionId;
    }
    if (role === "browser" && nativeAuto && native !== null) {
      native.sessionId = requestedSessionId;
    }
    return resultOk();
  }

  function recipientFor(record, type) {
    if (type === "offer") return record.role === "browser" ? native : null;
    if (type === "answer") return record.role === "native" ? browser : null;
    if (type === "ice" || type === "close") return record.role === "browser" ? native : browser;
    return null;
  }

  function validateRelay(record, message) {
    if (record.role === null || record.sessionId === null) return resultError("hello_required");
    if (message.sessionId !== record.sessionId) return resultError("session_mismatch");
    if (!RELAY_TYPES.has(message.type)) return resultError("unsupported_message");
    if ((message.type === "offer" || message.type === "answer") && !isValidSdp(message.sdp)) {
      return resultError("invalid_sdp");
    }
    if (message.type === "ice" && !isValidIceCandidate(message.candidate)) {
      return resultError("invalid_ice");
    }
    if (message.type === "offer" && record.role !== "browser") return resultError("role_not_allowed");
    if (message.type === "answer" && record.role !== "native") return resultError("role_not_allowed");
    return null;
  }

  function handleMessage(endpoint, rawMessage) {
    const record = endpoints.get(peerKey(endpoint));
    if (!record || record.endpoint !== endpoint) return resultError("not_attached");
    const raw = typeof rawMessage === "string" ? rawMessage : rawMessage?.toString?.("utf8");
    if (typeof raw !== "string") return resultError("invalid_message");
    if (Buffer.byteLength(raw, "utf8") > maxMessageBytes) return resultError("message_too_large");

    let message;
    try {
      message = JSON.parse(raw);
    } catch {
      return resultError("invalid_json");
    }
    if (message === null || typeof message !== "object" || Array.isArray(message) || typeof message.type !== "string") {
      return resultError("invalid_message");
    }
    if (message.type === "hello") return setRole(record, message.role, message.sessionId);

    const validationError = validateRelay(record, message);
    if (validationError !== null) return validationError;
    const recipient = recipientFor(record, message.type);
    if (recipient === null) return resultError("peer_not_connected");
    try {
      recipient.endpoint.send(JSON.stringify(message));
    } catch {
      return resultError("peer_send_failed");
    }
    return resultOk();
  }

  function snapshot() {
    return {
      sessionId,
      browserConnected: browser !== null,
      nativeConnected: native !== null,
      nativeAuto,
    };
  }

  return { attach, detach, handleMessage, snapshot };
}
