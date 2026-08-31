# CamBridge Windows Stage 1 HTTPS server

The server serves `web/client/` over HTTPS on a detected private LAN IPv4
address. The printed IP URL is the Stage 1 acceptance route. The server also
exposes a same-origin WSS signaling endpoint at `/signaling`; it only relays
bounded JSON Offer/Answer/ICE control messages and does not carry video. The
native WebRTC receiver connects to this endpoint as a separate peer; the server
still relays signaling only and never carries the video media path.

## First-time certificate setup

From the repository root in PowerShell, pass every private LAN IPv4 address
that the iPhone may use to reach this PC:

```powershell
.\scripts\generate-local-ca.ps1 -IPAddress 192.168.11.20
```

The script writes public `.cer` files and a password-protected `.pfx` under
`windows/stage1-server/certificates/`. That directory is Git-ignored. Transfer
only `cambridge-root-ca.cer` to the iPhone; never transfer the PFX or a private
key.

On iPhone, install the root certificate and then enable trust:

1. Open the installed profile/certificate.
2. Go to **Settings → General → About → Certificate Trust Settings**.
3. Enable full trust for **CamBridge Local CA** and confirm.

## Start

Omit the environment variable and enter the PFX password at the server's
interactive prompt. The value is not logged or stored by the server:

```powershell
npm ci --prefix .\windows\stage1-server
node .\windows\stage1-server\server.mjs `
  --pfx .\windows\stage1-server\certificates\cambridge-server.pfx `
  --certificate .\windows\stage1-server\certificates\cambridge-server.cer
```

The server prints bind address, HTTPS port, IP URL, actual certificate SAN,
mDNS/Bonjour status, Windows host `.local` status, and friendly URL status.
It does not change Windows Firewall policy. If Windows asks whether the
private-network connection should be allowed, review and allow only the
private LAN profile if appropriate.

The same HTTPS listener accepts WSS at `wss://<bind-ip>:<port>/signaling`.
Clients must send `hello` with role `browser` or `native` and a shared session
ID before sending `offer`, `answer`, `ice`, or `close`. The current endpoint
relays only bounded JSON control messages; media still does not pass through
this server. The native receiver connects to this endpoint as the separate
WebRTC peer; the server is not a media relay.

## Name resolution policy

`https://<private-LAN-IPv4>:8443` is the required Stage 1 route. Bonjour
service advertisement is optional discovery information and does not create
`cambridge.local`. The server reports a friendly `.local` URL only when the
actual Windows host `.local` name resolves and the certificate SAN contains
that exact name. A fixed `cambridge.local` alias is not claimed or installed.
