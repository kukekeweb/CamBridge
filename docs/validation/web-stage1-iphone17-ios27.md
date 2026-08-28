# Web Stage 1 実機Validation記録

## 対象

- 端末: iPhone 17
- OS: iOS 27
- Browser: Safari
- 検証対象: CamBridge Web ClientのCamera Capability Probe / Diagnostic Matrix

## 確定した結果

背面カメラのDiagnostic Matrixで、次の組み合わせが成立した。

| 要求 | getUserMedia | 実際のTrack | 実測FPS | 判定 |
| --- | --- | --- | --- | --- |
| 1920×1080 @ 60fps exact | 成功 | 1920×1080 @ 60fps | 1秒: 60、10秒: 約59.94 | match |

追加で次を確認した。

- `settings.frameRate = 60`
- `deviceIdMatches = true`
- 要求FPS基準の不足フレーム数 = 0
- 2560×1440 @ 30fpsが成立
- 3840×2160 @ 30fpsが成立
- H.264 capabilityが存在
- H.265 capabilityが存在
- VP8 / VP9 / AV1 capabilityが存在

## カメラ別の観察

- 背面デュアル広角: 720p60と1080p60が成立。1440p60と4K60は実際のTrackが30fps。
- 背面超広角: 720p60が成立。1080p60はTrack 60だが、10秒実測は約55.4fps。1440p60と4K60は30fps。
- 前面: 720p60は成立。1080p60、1440p60、4K60は30fps。
- 前面超広角: 60fps要求はすべて実際のTrackが30fps。
- 30fpsは1080p、1440p、4Kで成立。

## 推奨Stage 1プリセット

- 1920×1080 @ 30fps
- 1920×1080 @ 60fps
- 2560×1440 @ 30fps
- 3840×2160 @ 30fps

既定値は背面カメラ、1920×1080、60fpsとする。ただし、カメラ能力はdevice/lensごとに異なるため、要求値と実測値を常に表示する。

## Codec capabilityの扱い

`RTCRtpSender.getCapabilities("video")` がH.264、H.265、VP8、VP9、AV1を返したことは記録する。ただし、これはWebRTCで実際にnegotiationできたこと、またはhardware encoder経路が使用されたことを意味しない。Stage 2ではSDPの選択codec、sender statsのcodec、encoded frames、実際のencoder経路を別途確認する。

## Acceptance状態

上記は実機の短時間Diagnostic Matrix結果であり、1080p60取得能力の強い証拠である。Web Stage 1の正式PASSは、背面カメラを選択した1080p60 Stability Testを600秒完走し、Track状態・設定変化・ページライフサイクル・JavaScript errorを確認するまで保留する。

Stage 2のWebRTC sender、signaling、Windows receiver、decoder、D3D11、Virtual Camera実装は、この10分AcceptanceがPASSになるまで開始しない。
