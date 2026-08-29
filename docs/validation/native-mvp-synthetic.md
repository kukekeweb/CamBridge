# Native MVP Synthetic Validation

検証日: 2026-08-29

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

未実装:

- libdatachannel receiver
- WSS signaling
- H.264 RTP depacketization
- Media Foundation H.264 decode
- decoded NV12 publish integration
- D3D11 preview
- Discord acceptance

従って、Native MVPおよびStage 2合格とは判定しない。
