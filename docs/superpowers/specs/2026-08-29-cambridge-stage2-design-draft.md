# CamBridge Web Stage 2 Design Draft

## Scope and gate

これはStage 2の設計ドラフトであり、実装開始の承認ではない。Stage 1のiPhone 17 / iOS 27 Safari 1080p60 Stability Testが600秒PASSになった後に、別途設計を確認してから実装する。Stage 2はSafariからWindows Previewまでを扱い、Virtual CameraはStage 3として含めない。

## Target data flow

```text
iPhone Safari getUserMedia(1920×1080 exact @ 60)
  -> RTCPeerConnection sender
  -> LAN host-candidate WebRTC transport
  -> Windows native WebRTC receiver
  -> decoded video frame
  -> D3D11 preview
```

Capture、Encode、Transport、Decode、Previewを独立した境界として保つ。SafariのTrackを直接Windows出力へ結合せず、各境界の統計を別々に収集する。Stage 2ではVirtual Camera、Media Foundation source、kernel driverを追加しない。

## Windows HTTPS and WSS signaling

既存のWindows Local HTTPS serverを同一originのWeb Client配信に使用し、同じTLS証明書上でWSS signaling endpointを提供する。最初のsession modelは1 iPhoneと1 Windows receiverの一対一とする。

1. Windows serverが短命のsession identifierと接続状態を作る。
2. Safariが同一originのWSSへ接続する。
3. SafariがOffer SDPを送る。
4. WindowsがAnswer SDPを返す。
5. 両端が生成したICE candidateをWSSで交換する。
6. ICE connected / disconnected / failed / closedを両端で表示する。

映像本体はWSSを通さず、WebRTCのUDP media pathだけを使用する。signaling payloadにはOffer/Answer/ICE candidateとsession stateだけを許可し、任意の外部URLやcloud relay指定は受け付けない。WSSは同一origin、LAN bind、許可されたsession token、想定するmessage schemaを検証する。

## ICE and LAN security

- TURN serverは設定しない。
- 外部STUNも初期構成では設定しない。
- host candidateによる同一private LAN直接接続を第一条件とする。
- private IPv4を優先して表示し、IPv6やmDNS candidateはruntime結果として診断に残す。
- `iceTransportPolicy`やcandidateの実際の種類はruntime statsで確認し、host以外が選択された場合はLAN direct判定を不成立にする。
- Windows Firewallは自動変更しない。ユーザーがPrivate networkでCamBridgeアプリとWebRTC UDP ephemeral portを許可する必要条件を表示する。
- WSS/HTTPSはLAN interfaceへだけbindし、証明書private keyはWindows側に残す。iPhoneにはroot CA公開証明書だけを渡す。
- reconnectでは既存PeerConnectionを明示的にcloseし、古いsession/candidateを破棄して新しいOffer/Answer/ICE交換を開始する。無限高速retryは避け、段階的backoffと理由表示を行う。

## Codec selection and proof

Stage 2の第一候補はH.264。Safariのruntime `RTCRtpSender.getCapabilities("video")` 結果とWindows receiverのdecoder capabilityの共通部分だけを候補にする。`RTCRtpTransceiver.setCodecPreferences()`が使える場合は、H.264を優先して提示するが、API非対応時はSDPの実際の結果を採用する。

「H.264を要求した」だけでは対応確定としない。次を同じ接続のstatsから確認する。

- selected codec statsの`mimeType`とpayload mapping
- sender `framesEncoded` / `framesPerSecond` / `bytesSent`
- receiver `framesDecoded` / `framesPerSecond` / `framesDropped`
- SDPの実際のcodecとprofile/level
- Windows decoder factoryが選択した経路とhardware/software区分

H.265はSafari capabilityに存在するが、Stage 2後半の候補に留める。SDP negotiation、sender encoding stats、Windows decoder経路の三つが同一試験で確認されるまで、送信対応・hardware encode対応とは扱わない。VP8/VP9/AV1も同じruntime evidence原則に従う。

## Low-latency policy and observability

フレームを長いqueueへ蓄積せず、receiverは古いフレームを捨てて最新フレームを表示するbounded latest-frame bufferを基本にする。decoder前後で不要なRGB変換や複製を避け、D3D11へ渡す形式は実測したdecoder outputと変換コストを記録する。

記録する統計は次の通り。

- Safari: capture FPS、Track settings、outbound FPS、encoded frames、sent frames、bytes/bitrate
- WebRTC: selected codec、packets sent/received、packet loss、RTT、jitter、ICE candidate pair、connection state
- Windows: inbound FPS、received frames、decoded frames、dropped frames、decode time、jitter buffer delay、queue depth、render FPS、frame size
- 接続: reconnect count、connection duration、reason、推定end-to-end latency

Safariの`RTCRtpSender.getStats()`、receiver stats、jitter buffer関連property、target latency関連propertyは実行時に存在を検出する。未対応propertyをpolyfillしたり、存在を仮定して設定したりしない。取得できない値は「非対応/取得不可」として表示する。

## Windows implementation boundary

Windows native receiverはC++20を基本候補とし、libwebrtcのPeerConnection/receiverを中心にする。decoderはlibwebrtcの実際に選択可能なhardware decoder factoryを検出し、D3D11 textureまたは最小限のNV12/I420変換を使う。hardware経路が選べなかった場合はsoftware経路へ黙って置き換えず、選択経路とCPU/GPU統計を表示する。

Stage 2 Milestone 1は、Safari → WebRTC → Windows receiver → Previewの一接続に限定する。receiverは受信、統計、切断/reconnect、Previewまでを担当し、Virtual Camera出力は担当しない。

## Acceptance criteria draft

1. iPhone Safariが実機で1920×1080 @ 60fps Trackを開始できる。
2. SDP Offer/AnswerとICE candidate交換が同一LANのWSSだけで成立する。
3. selected candidate pairがprivate LAN directで、TURN/STUN/外部relayなしである。
4. Windows receiverが実際のselected codecとdecoder経路を表示する。
5. Windows Previewのdecoded/rendered FPSが継続して59〜60fpsである。
6. packet loss、RTT、jitter、frames dropped、queue depthを表示できる。
7. 接続断後に古いPeerConnectionを再利用せずreconnectできる。
8. 10分receiveでreceiver側のframe loss、decode error、reconnect、メモリ増加を記録する。
9. cloud signaling、TURN relay、外部video uploadが発生しない。

## Latency measurement

統計上のRTTやjitterは映像遅延そのものではないため、別の実測を行う。iPhone画面とWindows Previewを同じ240fps以上のカメラで同時撮影し、iPhone側の画面にミリ秒表示またはLED点滅を出して、同一イベントの表示フレーム差を数える。複数回の中央値、最小、最大、p95を記録する。画面撮影自体の露光・表示遅延を注記し、WebRTC statsのRTT/jitterとは混同しない。
