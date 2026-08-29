# CamBridge Native MVP / Web Stage 2 Design Draft

## Scope

本設計は、OBSを経由せず、iPhone SafariをWindowsのカメラデバイスとして直接公開するNative MVPを定義する。Web Stage 1の10分実機Acceptanceは維持するが、Native MVPの実装は並行して進める。Stage 2ではVirtual Cameraを前倒しし、D3D11 Previewは後続とする。

```text
iPhone Safari
  getUserMedia(1920x1080 exact @ 60)
        |
        | WebRTC / H.264 / LAN direct
        v
Native Receiver
  libdatachannel (first candidate)
  H.264 RTP depacketizer
  Media Foundation H.264 decoder
        |
        | Native Frame Publisher
        v
Latest NV12 Frame IPC
  shared memory + sequence + event
        |
        v
Custom Media Source (Frame Server process)
        |
        v
MFCreateVirtualCamera / CurrentUser
        |
        v
CamBridge Windows Virtual Camera -> Discord / Zoom / Browser
```

OBS、OBS Virtual Camera、Browser Receiver、Window Capture、kernel driver、DirectShow filter、TURN、外部STUN、cloud relayは使用しない。

## WebRTC stack selection

Native MVPの第一候補はlibdatachannelとする。Safari互換、ICE、DTLS、SRTP、WebSocket、H.264 RTP depacketizerを持ち、Windows CMake buildがlibwebrtcより小さいため、receiver-onlyの最短経路に適する。Decoderはlibdatachannelに任せず、Media Foundationへ分離する。

libwebrtcはSafari互換性、H.264、Stats、decoder integrationの幅が優れるが、Chromium依存のGN/Ninja buildと大規模な保守負荷がある。libdatachannelのSafari-H.264相互接続Probeで、Offer/Answer、ICE/DTLS/SRTP、H.264 onFrame、RTCP、reconnectに重大な問題が出た場合だけlibwebrtcへ切り替える。

libdatachannelで不足するW3C相当statsは、PeerConnection state、selected candidate pair、RTCP、receiver counters、decoder countersをCamBridge側で収集する。取得できない値は未取得として表示し、推測値を出さない。

## Virtual Camera registration and process boundary

`MFCreateVirtualCamera`の登録とCustom Media Source DLLのロードを別責務として扱う。

初回インストール:

- `CamBridge.exe --install`または将来の`Install-CamBridge.exe`
- Custom Media Source COM DLLと依存DLLを配置・登録
- `MFCreateVirtualCamera(..., MFVirtualCameraLifetime_System, MFVirtualCameraAccess_CurrentUser, ...)`で登録
- 必要な場合だけUACを要求し、理由を表示

通常起動:

- 管理者権限を要求しない
- HTTPS/WSS、Native Receiver、Virtual Cameraを開始
- iPhone待機状態を表示

まずHKCU / CurrentUserだけでFrame ServerがCustom Media Sourceをロードできるか検証する。失敗時は、DLLのシステム登録だけを初回UAC工程へ分離し、RuntimeのVirtual Camera accessはCurrentUserに維持する。登録済みと、Discord等から実際にopenできることを別々に合格判定する。

WindowsがFriendly NameへVirtual Camera識別文字列を追加する可能性があるため、完全一致を要求しない。列挙時はCamBridgeの固定CLSID、SourceId、Virtual Cameraのdevice identity、公開MediaTypeを検証し、名称は`CamBridge Windows Virtual Camera`等を許容する。

Custom Media SourceはFrame Server側の別プロセスでロードされ得るため、`CamBridge.exe`内のC++オブジェクトを直接参照しない。

## Latest-frame IPC

映像本体は名前付き共有メモリを第一候補とする。固定サイズのNV12領域を二面または三面で持ち、headerのatomic sequence、slot sequence、width、height、stride、timestamp、producer stateを公開する。Producerは書き込み完了後にsequenceをrelease storeし、Eventでconsumerを通知する。Consumerは最新sequenceだけを読み、遅れた場合に古いframeを追いかけない。

Named Pipeはcontrol、connection state、format metadata、install/diagnostic commandに限定し、毎frameの映像コピーには使用しない。Frame Serverプロセスから共有メモリを開けない場合は、原因を記録した上で、最小限のsingle-latest shared buffer方式を再評価する。大きなqueue、無制限copy、RGB変換は許可しない。

No-signal時はVirtual Cameraを停止せず、黒色NV12 frameを供給する。Placeholder文字を描画する機能はMVP後とする。

## Synthetic-first implementation order

1. Native build environment and Windows build-number check
2. Synthetic 1920x1080 NV12@60 producer
3. Latest-frame shared memory IPC
4. Custom Media Source
5. Media Source unit tests
6. CurrentUser Virtual Camera registration
7. Windows Media Foundation capture client enumeration/open
8. Synthetic 1080p60 capture from CamBridge Virtual Camera
9. Frame Server IPC isolation test
10. libdatachannel receiver skeleton
11. Safari signaling interoperability Probe
12. H.264 RTP depacketization and access-unit tests
13. Media Foundation H.264 decoder and hardware-path evidence
14. Decoded NV12 publish through IPC
15. Safari video through CamBridge Virtual Camera
16. Discord device selection and preview test

Synthetic steps 2-9 must be independently green before WebRTC media is connected.

## Signaling and LAN policy

Existing HTTPS server receives a same-origin WSS endpoint such as `/signaling`. The only peer model is one Safari sender and one Windows receiver. Messages are schema-validated `hello`, `offer`, `answer`, `ice-candidate`, `state`, and `error`; SDP, ICE, and media are not persisted.

Use `iceServers: []` by default. Prefer private IPv4 host candidates and verify the selected pair. No TURN or external STUN is configured. Safari mDNS candidates are recorded and tested rather than assumed resolvable. Windows Firewall is not changed automatically; the required Private network permission and UDP port range are shown to the user.

Reconnect closes the old PeerConnection, discards its session and candidates, and starts a fresh signaling exchange with bounded backoff.

## Codec and decoder evidence

Safari runtime capabilities are only candidates. H.264 is preferred by runtime codec evidence and actual SDP negotiation. The selected codec must be proven by SDP, receiver track codec mapping, RTP counters, and decoder input.

H.264 flow:

```text
Safari hardware encoder (not assumed)
  -> WebRTC SRTP
  -> libdatachannel H264RtpDepacketizer
  -> H.264 access unit
  -> Media Foundation H.264 decoder
  -> NV12
```

Media Foundation hardware MFT is enumerated first. The selected transform name/CLSID, input/output subtype, timestamps, errors, and hardware/software classification are logged. Software fallback is explicit and never silently reported as hardware.

H.265 remains design-only until actual Safari negotiation and Windows decoder evidence exist.

## Low-latency and observability

Every boundary records FPS, frame count, drops, processing time, errors, sequence, and timestamps. WebRTC records selected codec, ICE pair, packets, loss, RTT, jitter, RTCP, and connection state. Decoder records input/output/drop/error and transform path. IPC records latest sequence, producer/consumer age, overwrite count, and queue depth (target 0 or 1). Virtual Camera records requested/output MediaType and sample delivery.

The acceptance report must distinguish camera-frame drops, requested-FPS deficiency, network loss, decoder drops, IPC overwrites, and consumer backpressure.

## Acceptance criteria

### Synthetic gate

- CamBridge Virtual Camera can be registered on Windows build >= 22000.
- Initial registration reports whether UAC was required.
- Media Source loads in the Frame Server process.
- Windows capture client enumerates CamBridge identity and opens it.
- Synthetic 1920x1080 NV12@60 is captured for 10 minutes with no fatal Media Source errors.
- Latest-frame overwrite behavior is observed under an intentionally slow consumer.

### Native MVP gate

- iPhone 17 / iOS 27 Safari rear camera sends 1920x1080@60.
- WebRTC is LAN-direct with no cloud, TURN, or external STUN.
- Negotiated codec is proven as H.264.
- H.264 is decoded on Windows and actual decoder path is logged.
- Decoded NV12 reaches the shared-memory publisher.
- CamBridge Virtual Camera supplies the frames without RGB conversion in the normal path.
- Discord or another capture client can select CamBridge and display video without OBS.
- Reconnect works and 10-minute receive has no fatal errors.

## Status

This document is an implementation specification after user approval. It does not claim that the Native MVP, Virtual Camera registration, Safari interop, decoder, or Discord enumeration has already passed.
