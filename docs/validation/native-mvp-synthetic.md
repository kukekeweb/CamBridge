# Native MVP Synthetic Validation

検証日: 2026-08-30

この文書は、Web Stage 1の実機検証結果とは分離した、Windows Native MVPの合成入力検証記録である。Safari、WebRTC、H.264 RTP、デコーダ、Discordはこの検証に含めない。

## Environment

- OS ProductName: `Windows 10 Home`（表示文字列）
- OS build: `26200.8457`
- Virtual Camera capability gate: build `26200 >= 22000`
- Windows SDK: `10.0.26100.0`
- Visual Studio Build Tools: 2022 / MSVC `14.44.35207`
- Architecture: x64
- 実行ユーザー: 標準ユーザー（管理者ではない）

## Build and unit tests

実行したコマンド:

```powershell
cmake -S windows/native-mvp -B build/native-mvp -G "Visual Studio 17 2022" -A x64
cmake --build build/native-mvp --config Release --parallel
ctest --test-dir build/native-mvp -C Release --output-on-failure
```

結果:

- configure: 成功
- Release build: 成功
- `cambridge_frame_ipc_tests`: 成功
- `cambridge_media_source_tests`: 成功
- CTest: 2/2 passed
- DLL export link warning (`LNK4104`): 発生。`dumpbin /exports`で4つのCOM DLL exportが存在することは別途確認済み。warning解消は未完了。

## Cross-process IPC

`cambridge_synthetic_publisher.exe`を別プロセスで起動し、`cambridge_frame_ipc_probe.exe`から共有メモリを読み取った。

```text
IPC probe: open=true
IPC probe: frames=121 lastSequence=152 observedFps=60.4676
IPC probe exit=0
```

判定:

- mapping open: 成功
- 別プロセスReaderからNV12 frame取得: 成功
- sequence単調増加: 成功
- 最新frame方式: 実装済み
- 60fps相当の合成Publisher/Reader経路: 単体確認済み

この結果は共有メモリIPCの検証であり、Virtual Cameraからのframe取得を意味しない。

追加診断:

- Publisherに`--duration-ms`を指定した有限時間起動を追加した。
- Publisherは起動時に`IPC ready`を表示する。
- IPC probeはproducer stateとlatest sequenceを表示する。
- Publisher稼働中のIPC probeで、約60fps、単調増加sequence、`producerState=1`を再確認した。

## Custom Media Source

`cambridge_media_source_tests.exe`でDLLを直接ロードし、COM class factory、activation、presentation descriptor、media type一覧、source start/stopを確認した。

```text
Media Source test: types=3 start=0x0
```

判定:

- COM DLL直接ロード: 成功
- `DllGetClassObject`: 成功
- `IMFActivate`生成: 成功
- media type: 3種類（1080p60 / 1080p30 / 720p60）
- `IMFSampleAllocatorControl` / `UsesProvidedAllocator`契約: 成功
- Media Source start/stop: 成功
- Windows Frame Serverからのロード: 未確認

### Synthetic sample path diagnosis

最初のcapture probeは同期`IMFSourceReader::ReadSample`を無期限に待つ実装で、CamBridge列挙後に終了しなかった。Frame Server logでは、`Start`までは到達するが`RequestSample`以降の観測が無かった。

公式SimpleMediaSourceとの差分を一つずつ検証し、CamBridgeの`Start`がsource event queueへ`MENewStream`/`MEUpdatedStream`を送らず、`MEStreamStarted`と`MESourceStarted`だけを送っていたことを確認した。修正前の回帰テストは、Start後の先頭eventがstream announcementでないため失敗した。`MENewStream`をstream開始前に追加した後、次の直接Media Sourceテストが成功した。

```text
Media Stream sample test: time=5166677 duration=166666 bytes=3110400
Media Source test: types=3 start=0x0
```

Publisher稼働中はMedia Sourceのcontrol logで以下を確認した。

```text
Initialize.ipc mappingOpen=1 producerState=1 publishedSequence=31
Start.format width=1920 height=1080 fps=60 denominator=1
RequestSample.begin
CreateSample.ipc mappingOpen=1 producerState=1 publishedSequence=31 lastReadSequence=31
SampleCreated sampleIndex=1 sequence=31 timestamp100ns=5166677 bufferBytes=3110400
SampleDelivered sampleIndex=1 sequence=31 timestamp100ns=5166677 bufferBytes=3110400
Shutdown.summary requestSamples=1 samplesProduced=1 samplesDelivered=1 lastSequence=31
```

この直接テストは、Media Source自身のsample生成・event配送とPublisher IPCの接続を検証する。Frame Server経由のVirtual Camera capture PASSを意味しない。

### Bounded capture probe

capture probeはcapture処理を子プロセスへ隔離し、親が既定10秒（`--timeout-ms`で変更可能）で監視する構成へ変更した。`MFCreateSourceReaderFromMediaSource`または`ReadSample`がブロックしても、probe全体は永久待機しない。timeout時は次を表示する。

- `Sample delivery timeout`
- Media Source / Stream / RequestSample / sample creation / deliveryのcontrol log場所
- `mappingOpen`
- IPC open error
- producer state
- latest synthetic sequence

修正版のprobeを、更新前のProgram Files DLLに対して実行した結果は以下である。

```text
Video input count: 1
[0] CamBridge (Windows ?????)
CamBridge camera found: YES
Sample delivery timeout: 1000 ms
IPC state: mappingOpen=true openError=0x0 producerState=1 latest synthetic sequence=103
Synthetic/sample probe: 0 samples
```

これはtimeout処理とPublisher IPC観測の成功であり、更新版DLLをFrame Serverへ再登録した後のcapture成功とは分離する。更新版DLLによるProgram Files/HKLM登録後のlive capture再検証が残っている。

### Latest control-path evidence

2026-08-30のcapture probeで生成されたMedia Source logには、次の順序が記録された。

```text
ActivateObject
Initialize.ipc mappingOpen=1 producerState=1
CreatePresentationDescriptor
Start.format width=1920 height=1080 fps=60
RequestSample.begin
CreateSample.ipc mappingOpen=1 producerState=1
SampleCreated sampleIndex=1 sequence=... bufferBytes=3110400
SampleDelivered sampleIndex=1 sequence=... bufferBytes=3110400
```

この結果から、Publisherの共有メモリopen、Media Source activation、stream start、最初のRequestSample、NV12 1080p sample生成、Media Stream event queueへの配送までは到達している。capture probeのSourceReader側では同じ実行でsample取得完了を確認できず、bounded timeoutとなった。

なお、その実行時のHKLM登録先は `C:\Program Files\CamBridge\Native\cambridge_media_source.dll`（ビルド直後の `build\\native-mvp\\Release\\cambridge_media_source.dll` とは別artifact）だった。したがって、更新版のstream-announcement修正をProgram Files/HKLMへ反映した後に、同じprobeを再実行する必要がある。現時点ではSynthetic Virtual Camera captureをPASSとは判定しない。

## CurrentUser Virtual Camera live gate

標準ユーザーで次を実行した。

```powershell
.\cambridge_virtual_camera_manager.exe --install --source .\cambridge_media_source.dll
```

実測結果:

```text
MFIsVirtualCameraTypeSupported: 0x0 (success)
Per-user Custom Media Source registration: 0x0 (success)
CoCreateInstance(IMFActivate): 0x0 (success)
MFCreateVirtualCamera(CurrentUser): 0x0 (success)
Virtual Camera identity properties: 0x80070005 (failure)
Virtual Camera identity properties require elevation; continuing to Start for CurrentUser diagnostics.
IMFVirtualCamera::Start: 0x80004002 (failure)
Video input count: 0
```

判定は未達である。

- A: HKCU / CurrentUserのCOM登録書き込み: 成功
- B: `MFCreateVirtualCamera(CurrentUser)`: 成功
- C: identity propertiesは`E_ACCESSDENIED`。標準ユーザーではmetadata書き込みに権限がなく、Start可否と分離して継続した
- D: `IMFVirtualCamera::Start`: `E_NOINTERFACE`で失敗
- E: Media Foundation capture clientのCamBridge列挙: 0台
- F: Synthetic 1080p60をVirtual Cameraから取得: 未達
- G: Discord / Zoom列挙: 未確認

この標準ユーザーセッションは管理者ではないため、HKLMの一回限り登録（`--machine`）はまだ実行していない。次の切り分けとして、ユーザーが明示的に許可した実機Windows上で、管理者PowerShellから一度だけ以下を試す。

```powershell
.\cambridge_virtual_camera_manager.exe --install --source .\cambridge_media_source.dll --machine
```

その後は通常の非管理者起動で、capture probeの列挙・open・sample取得を確認する。UACを要求するインストーラやファイアウォール変更は現時点では追加していない。

## Scope status

実装済み:

- bounded latest-frame NV12 shared memory
- synthetic 1920x1080 NV12 producer
- Custom Media Source DLLの単体経路
- CurrentUser / machine registration commandの分離
- actual Windows build number gate
- Media Foundation capture probe skeleton
- bounded capture probe with first-sample diagnostics
- Media Source stream-announcement event contract
- Publisher/IPC readiness output and installed IPC probe

未実装:

- libdatachannel receiver
- WSS signaling
- H.264 RTP depacketization
- Media Foundation H.264 decode
- decoded NV12 publish integration
- D3D11 preview
- Discord acceptance

従って、Native MVPおよびStage 2合格とは判定しない。
