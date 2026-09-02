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

### 2026-08-30 capture probe failure diagnosis

手動capture probeおよびcapture childを直接実行して、最新ログ
`C:\ProgramData\CamBridge\logs\media-source-3284.log`（Frame Server PID 3284）を確認した。
今回の対象試行は次の順序で終了している。

```text
Start.begin                         0x00000000
Start.format                        1920x1080 @ 60/1
Start.end                           0x00000000
RequestSample.begin                 0x00000000
RequestSample.CreateSample          0xC00D36B6
```

`CreateSample`以降の `CreateSample.ipc`、`SampleCreated`、`SampleDelivered`、timestamp、
duration、buffer copyは記録されていない。従って、この試行ではPublisherのsequenceや
共有メモリ読み取りが一次障害ではなく、sample生成開始前に停止している。

capture childの直接出力でも次を確認した。

```text
MFCreateSourceReaderFromMediaSource: 0x0 (S_OK)
IMFSourceReader::SetCurrentMediaType(NV12 1920x1080@60): 0x0 (S_OK)
IMFSourceReader::GetCurrentMediaType: 0x0 (S_OK)
SourceReader selected media type: subtype=NV12 width=1920 height=1080 fps=60/1
IMFSourceReader::ReadSample: 0xC00D36B6 (MF_E_NOT_INITIALIZED)
stream=4294967292 flags=0x1 timestamp=0 sample=no
Samples received: 0
```

`0xC00D36B6` はWindows SDKの `MF_E_NOT_INITIALIZED` である。公式
`SimpleMediaStream` は `StartInternal` 内で `MFCreateVideoSampleAllocatorEx`（必要時）と
`InitializeSampleAllocator(10, mediaType)` を完了してから `RequestSample` で
`AllocateSample` を呼ぶ。CamBridgeにはこのStart時初期化がなく、Frame Serverで
`MFCreateSample` / allocator経路が未初期化のままsample作成へ進んでいた。

この仮説を分離するため、Media Sourceテストに「内部allocator未提供」と
「Frame Server相当のprovided allocator」の2経路を追加した。修正前は両方が
`MF_E_NOT_INITIALIZED`で失敗し、Start時allocator初期化後は両方が成功した。
修正は `cc2bad8`（`fix: initialize virtual camera sample allocator`）に記録した。
この修正はまだProgram Filesへ再インストールしていないため、実機Frame Serverの
再probe成功は未確認である。

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

### In-use DLL installation guard

旧DLLがFrame Serverにロードされた状態で同じファイルを`Copy-Item -Force`すると、次の失敗が発生した。

```text
Copy-Item : 別のプロセスで使用されているため、プロセスはファイルにアクセスできません。
```

旧launcherはこの失敗後もmanagerを起動していたため、旧DLLのままregistration/probeが実行される余地があった。installerを修正し、通常名のDLLが使用中の場合はProgram Files内へversioned DLLをside-by-side配置し、`cambridge_media_source.active.txt`へ実際の登録パスを保存するようにした。artifact copyが失敗した場合は、machine registration・publisher・capture probeを実行せず終了する。

旧launcherの失敗後に残ったProgram Files版Synthetic Publisherが、次回実行時に自身のexeをロックしていた事例も確認した。installerはコピー前に、実行パスがCamBridge install root配下であるPublisherだけを停止する。パスを取得できない別プロセスは停止せず、誤って他のPublisherを終了しない。

## Capture child Media Foundation lifecycle revalidation

The capture probe was instrumented so the child process initializes its own COM and Media Foundation runtime before touching Media Foundation APIs. A direct probe run produced:

```text
CoInitializeEx: 0x0 (success; S_OK)
MFStartup version: 0x20070
MFStartup(MF_VERSION): 0x0 (success; S_OK)
MFCreateSourceReaderFromMediaSource: 0x0 (success; S_OK)
SetCurrentMediaType: 0x0 (success; S_OK)
GetCurrentMediaType: 0x0 (success; S_OK)
Selected type: NV12 1920x1080 fps=60/1
IMFSourceReader::ReadSample: 0xc00d36b6 (failure; MF_E_NOT_INITIALIZED)
Samples received: 0
MFShutdown: executed
CoUninitialize: executed
```

This proves that the capture child performs `MFStartup(MF_VERSION)` successfully and pairs it with `MFShutdown`; the child-process initialization omission is therefore not the cause of the observed `ReadSample` failure. The failure remains on the Media Source / Frame Server side of the sample-delivery boundary. No Media Source, allocator, IPC, or WebRTC change is made by this diagnostic commit.

The requested A/B experiment (“without `MFStartup` must return `MF_E_NOT_INITIALIZED`, with it must not”) was not added as a passing regression test because the Media Foundation enumeration/sample APIs exercised on this Windows host returned `S_OK` even without an explicit startup. Committing that assertion would encode a false platform assumption. The reliable regression evidence is the child’s explicit startup/shutdown output above and the existing Media Source tests.

## Allocator identity diagnostic

The Media Source diagnostic now records the stream pointer, allocator pointer, allocator source, media type pointer, subtype, dimensions, and frame rate for allocator control-path calls and the first three sample requests. The local tests showed both supported paths using the same allocator instance through initialization and allocation:

```text
provided: SetSampleAllocator.end allocator=A
provided: InitializeSampleAllocator allocator=A -> S_OK
provided: RequestSample.allocator allocator=A
provided: AllocateSample allocator=A -> S_OK

internal: MFCreateVideoSampleAllocatorEx allocator=A
internal: InitializeSampleAllocator allocator=A -> S_OK
internal: RequestSample.allocator allocator=A
internal: AllocateSample allocator=A -> S_OK
```

The independent identity regression test also models an initialized allocator A being replaced by an uninitialized allocator B; B returns `MF_E_NOT_INITIALIZED` from `AllocateSample` as expected. This establishes the symptom model, but it does not yet identify the allocator used by the Frame Server run. The new diagnostic DLL must be installed under the registered machine path before the next real capture probe.

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

### Current dependency preflight (2026-08-31)

The repository `vcpkg.json` manifest and its pinned baseline were installed on
the current Windows host with the Visual Studio 2022 bundled vcpkg executable:

```text
vcpkg version: 2025-11-19-da1f056dc0775ac651bea7e3fbbf4066146a55f3
libdatachannel[core,srtp,ws]:x64-windows@0.24.5: installed
All requested installations completed successfully in: 4.4 min
```

This is a dependency preflight only. It does not replace the Windows GitHub
Actions build or the live Frame Server/Virtual Camera acceptance.

> Historical note: the initial sections below preserve the earlier pre-restart
> and pre-gate observations. The current authoritative result is the bounded
> PASS recorded in
> [media-source-event-trace-2026-08-31.md](media-source-event-trace-2026-08-31.md).
> The historical `E_NOINTERFACE`/0-sample observations must not be read as the
> current installed-state result.

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

未完了:

- Safariとのlibdatachannel ICE/DTLS/SRTP接続
- 実H.264 RTP受信のend-to-end確認
- decoded NV12 publish integration
- D3D11 preview
- Discord acceptance

実装済みだがlive接続未確認:

- libdatachannel receiver adapter
- WSS signaling wrapper
- H.264 RTP depacketizer fixture boundary
- Media Foundation H.264-to-NV12 decoder boundary

従って、Native MVPおよびStage 2合格とは判定しない。

## Current authoritative bounded gate result

The historical failure sections above preserve the earlier pre-restart and
pre-current-artifact observations. The current authoritative installed-artifact
result is the bounded repeat recorded on 2026-08-31 04:33 JST:

```text
CamBridge camera found: YES
SourceReader selected media type: subtype=NV12 width=1920 height=1080 fps=60/1
ReadSample #1: S_OK, MF_SOURCE_READERF_STREAMTICK, sample=no
ReadSample #2: S_OK, flags=0, sample=yes
Samples received: 120
IMFMediaSource::Shutdown: S_OK
Capture child exit: 0
Synthetic/sample probe: 120 samples
Residual synthetic publisher processes: 0
```

The corresponding capture child diagnostic is
`C:\Users\kukeke\AppData\Local\Temp\CamBridge-capture-child-27024-41186640.log`.
The initial stream tick is an expected SourceReader notification; it is not a
CamBridge-queued `MEStreamTick` and did not prevent subsequent sample delivery.
This gate proves Synthetic Publisher/IPC → Media Source/Frame Server →
Virtual Camera → SourceReader for NV12 1920x1080@60. It does not prove Safari
WebRTC interoperability, hardware decode, or Discord acceptance.

## Current installed-artifact repeat (2026-08-31, bounded)

The installed Program Files Publisher and capture probe were run once more
without changing registration, Media Source code, or Frame Server state. The
Publisher was launched as PID `1544`; the separate IPC probe passed before the
capture probe (`mappingOpen=true`, `producerState=1`, observed approximately
60 fps). Cleanup stopped that exact Publisher PID and found no residual
`cambridge_synthetic_publisher` process.

The capture probe reported:

```text
Video input count: 1
CamBridge camera found: YES
SourceReader selected media type: subtype=NV12 width=1920 height=1080 fps=60/1
ReadSample #1: S_OK, MF_SOURCE_READERF_STREAMTICK, sample=no
ReadSample #2: S_OK, flags=0, sample=yes
sample[1]: duration=166666, bufferBytes=3110400
sample[2]: duration=166666, bufferBytes=3110400
Samples received: 120
IMFMediaSource::Shutdown: S_OK
Capture child exit: 0
Synthetic/sample probe: 120 samples
```

The capture-child diagnostic was written to
`C:\Users\kukeke\AppData\Local\Temp\CamBridge-capture-child-23156-44191062.log`.
This is a second bounded installed-artifact PASS, not a WebRTC or iPhone-camera
result.

## Current-source bounded gate repeat (2026-09-02 23:37 JST)

The current checkout was revalidated against the already registered CamBridge
Virtual Camera without changing Frame Server registration or adding WebRTC
behavior to the Media Source. A finite Synthetic Publisher was started, its
shared-memory IPC readiness was checked, and the capture probe was run with its
existing bounded timeout. The publisher was stopped by PID after the probe.

Observed result:

```text
CamBridge camera found: YES
SourceReader selected media type: subtype=NV12 width=1920 height=1080 fps=60/1
ReadSample #1: S_OK, MF_SOURCE_READERF_STREAMTICK, sample=no
ReadSample #2: S_OK, flags=0, sample=yes
Samples received: 120
Capture child exit: 0
Synthetic/sample probe: 120 samples
Residual synthetic publisher processes: 0
```

The bounded capture child and publisher diagnostics are retained under
`build/native-mvp/diagnostics/synthetic-gate/` for this run. This is the
authoritative current-source repeat of the Synthetic Publisher → IPC → Media
Source/Frame Server → Virtual Camera → SourceReader gate. It does not claim
Safari WebRTC reception, live-camera image content, hardware decode, or Discord
acceptance.

The native receiver may run at the same time for the next gate; its waiting
state is expected until Safari sends an Offer. A receiver log with
`rawRtpPackets=0`, `accessUnits=0`, and `peerState=new` proves only that no
browser session has arrived yet. It is not evidence of a regression in this
synthetic gate.
