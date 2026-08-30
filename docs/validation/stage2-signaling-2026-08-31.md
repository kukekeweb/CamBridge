# Stage 2 signaling transport validation (2026-08-31)

## Implemented boundary

The existing local HTTPS server now accepts same-origin WebSocket upgrades at
`/signaling`. The TLS listener, certificate, bind address, and port remain the
same as the Stage 1 web server. The signaling layer contains no media bytes and
does not contact a cloud, STUN, or TURN service.

The broker supports one browser endpoint and one native endpoint per session:

- `hello`: binds role and session ID;
- `offer`: browser to native;
- `answer`: native to browser;
- `ice`: relayed between endpoints;
- `close`: relayed before the WebSocket closes.

Messages are JSON, bounded to 512 KiB at the WebSocket and broker boundary, and
invalid JSON, invalid roles/session IDs, invalid SDP/ICE, duplicate roles, and
messages before `hello` are rejected with a stable error code. A detached pair
can establish a fresh session.

## Tests

Run from `windows/stage1-server`:

```text
npm ci
npm test
npm run check
```

Observed result:

```text
6 tests passed, 0 failed
```

The integration test exercises a real local WebSocket upgrade and relays
Offer/Answer/ICE. It uses an HTTP test listener to avoid repository certificate
fixtures; the production path is the same upgrade handler attached to the
existing HTTPS listener.

## Not yet proven

This validation does not prove Safari WebRTC interoperability, ICE/DTLS/SRTP,
H.264 negotiation, RTP reception, decoding, or frame publication. Those are
separate gates in the Stage 2 native WebRTC plan.
