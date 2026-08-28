# CamBridge

CamBridge is a LAN-only iPhone camera project. The active iPhone client is a
Safari Web Client; the existing Swift/SwiftUI/AVFoundation implementation in
[`ios/`](ios/) is retained as a legacy/reference implementation and is not
deleted.

## Current scope: Web Stage 0 + Stage 1

Stage 1 serves a camera capability probe from the Windows PC over local HTTPS.
It does not send video to Windows yet. WebRTC media sending, WSS signaling,
Windows receiving/decoding, D3D11 rendering, and Virtual Camera output are
reserved for later stages.

The required test URL is the private-LAN IPv4 URL printed by the server:

```text
https://<LAN IPv4>:8443
```

Do not use `localhost` from iPhone Safari. The server never assumes that a
Bonjour service registration creates the fixed hostname `cambridge.local`.

## Local HTTPS setup on Windows

1. Find the private IPv4 address of the Windows PC and generate a certificate
   containing every address the iPhone may use:

   ```powershell
   .\scripts\generate-local-ca.ps1 -IPAddress 192.168.11.20
   ```

2. Transfer only
   `windows/stage1-server/certificates/cambridge-root-ca.cer` to the iPhone.
   Never transfer the server PFX, private key, or PFX password.
3. Install the certificate profile on the iPhone.
4. Open **Settings → General → About → Certificate Trust Settings** and enable
   full trust for **CamBridge Local CA**.
5. Start the server. It prompts for the PFX password without logging it:

   ```powershell
   node .\windows\stage1-server\server.mjs `
     --pfx .\windows\stage1-server\certificates\cambridge-server.pfx `
     --certificate .\windows\stage1-server\certificates\cambridge-server.cer
   ```

6. Open the printed `iPhone access URL` in Safari on the same private LAN.

The server prints bind address, HTTPS port, the IP access URL, actual
certificate SAN, mDNS/Bonjour status, the actual Windows host `.local` status,
and friendly URL status. It does not change Windows Firewall policy. Review
any Windows private-network firewall prompt manually.

## Name resolution policy

The IP URL is the Stage 1 baseline and does not depend on mDNS. If
`dns-sd.exe` is available, the server may advertise the informational
`_cambridge._tcp.local.` service. Service discovery and hostname resolution
are separate: `dns-sd.exe -R` alone does not publish `cambridge.local`.

The server reports a `.local` friendly URL only when the actual Windows host
name resolves on the LAN and the certificate SAN covers that exact name. No
hosts-file, router DNS, firewall, or fixed alias is installed automatically.

## Web Client diagnostics

The page exposes:

- camera video inputs from `enumerateDevices()`;
- returned track capabilities, settings, and constraints;
- exact resolution/FPS requests, with no silent fallback;
- `requestVideoFrameCallback()` one-second and ten-second measured FPS,
  frame counter, and missing-frame estimate;
- runtime `RTCRtpSender.getCapabilities("video")` codec capabilities;
- runtime low-latency API presence detection, without creating a peer
  connection.

The **Diagnostic Matrix Probe** separately tests every detected camera with
fresh exact requests for 720p30/60, 1080p30/60, 1440p30/60, and 4K30/60. A
successful mismatch is intentionally observed for ten seconds before its
track is stopped. Results include capability snapshots, actual settings,
constraints, measured 1-second/10-second FPS, missing-frame estimates,
exception details, and A/B/C diagnosis; JSON and CSV can be copied from the
page.

The diagnoses distinguish these causes:

- A: `frameRate.max < 60`, actual track is below 60.
- B: `frameRate.max >= 60`, requested 60, actual track is below 60.
- C: `frameRate.max >= 60`, actual track is 60, but measured 10-second FPS is
  substantially lower.

The 1920×1080 / 60 FPS result is accepted only from an iPhone 17 / iOS 27
Safari session's actual capabilities, settings, measured frames, and codec
list. A simulator or published device specification is not an acceptance
result.

## Validation

```powershell
npm test --prefix web/client
npm run check --prefix web/client
node --check windows/stage1-server/server.mjs
.\scripts\validate-web-stage1.ps1
```

GitHub Actions runs the same checks on a Windows runner in
`.github/workflows/web-stage1.yml`. Generated certificates, PFX files, private
keys, IPA files, and build output are ignored by Git.

## Stage 1 acceptance checklist

On the target iPhone 17 / iOS 27 Safari:

1. The printed IP URL opens without a certificate warning after trust setup.
2. Camera permission succeeds and video preview appears.
3. Video inputs are enumerated, with labels after permission where Safari
   provides them.
4. 1920×1080 / 60 is requested with exact constraints.
5. `getSettings()` shows the actual dimensions and FPS, or the UI says
   `1080p60 unavailable` / mismatch.
6. Measured FPS reports approximately 59–60 FPS when the API is available.
7. Capture remains stable for at least ten minutes.

For the 1080p60 investigation, run **Run All Cameras** after permission has
populated the device labels. Wait for all eight trials per camera to finish,
then use **Copy JSON** or **Copy CSV** to preserve the complete evidence.

Until this real-device gate passes, Stage 2 must not begin.
