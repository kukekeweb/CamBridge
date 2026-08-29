# Native MVP Media Source Interface Audit

更新日: 2026-08-29

## 目的と境界

現在の一次障害は、`MFCreateVirtualCamera(CurrentUser)` が成功した後の
`IMFVirtualCamera::Start()` が `0x80004002 (E_NOINTERFACE)` を返すことです。
この文書は、Microsoft Windows-Camera Virtual Camera sample の
`SimpleMediaSource` / `SimpleMediaStream` と CamBridge の契約差分を記録します。
この段階では不足している可能性があるインターフェースを推測で追加せず、
Frame Server の control path を観測して原因を分離します。

参照:

- [Microsoft Windows-Camera Virtual Camera sample](https://github.com/microsoft/Windows-Camera/tree/master/Samples/VirtualCamera)
- [MFCreateVirtualCamera](https://learn.microsoft.com/en-us/windows/win32/api/mfvirtualcamera/nf-mfvirtualcamera-mfcreatevirtualcamera)

## Interface 差分

### Media Source

| Contract | Microsoft SimpleMediaSource | CamBridge | 監査結果 |
|---|---|---|---|
| `IMFMediaSourceEx` | 実装 | 実装 | QI test 対象 |
| `IMFMediaSource` | 継承メソッドを明示実装 | 継承メソッドを明示実装 | 直接 activation test 済み |
| `IMFMediaEventGenerator` | 継承メソッドを明示実装 | 継承メソッドを明示実装 | QI test 対象 |
| `IMFGetService` | 実装 | 実装 | QI test 対象 |
| `IKsControl` | 実装 | 実装 | QI test 対象 |
| `IMFSampleAllocatorControl` | 実装 | 実装 | `UsesProvidedAllocator` を返す |
| source attributes | activation attributes をコピーし、sensor profile collection を追加 | 同等のコピーと Legacy/HighFrameRate profile を追加 | 契約を比較中 |
| stream attributes | source/stream の属性を公開 | source/stream attributes と descriptor attributes を公開 | descriptor 属性を追加済み |

CamBridge は WRL `RuntimeClass` の複数インターフェース実装で、公式 sample は
`winrt::implements` を使用しています。実装方式は異なりますが、必要な COM
interface の集合は同じです。`IMFMediaSource` は `IMFMediaSourceEx` のサブオブジェクト
として直接返すため、`ActivateObject(IID_IMFMediaSource)` には明示的な base cast を
使用しています。

### Media Stream

| Contract | Microsoft SimpleMediaStream | CamBridge | 監査結果 |
|---|---|---|---|
| `IMFMediaStream2` | 実装 | 実装 | QI test 対象 |
| `IMFMediaStream` | 継承メソッドを実装 | 継承メソッドを実装 | QI test 対象 |
| `IMFMediaEventGenerator` | 継承メソッドを実装 | 継承メソッドを実装 | QI test 対象 |
| stream descriptor attributes | descriptor 自体へ category/id/shared/frame-source-types を設定 | descriptor と stream attributes の両方へ設定 | contract test 済み |
| provided allocator | media type で allocator を初期化してから使用 | allocator を受け取り、現行実装は初期化経路を監査中 | 有力な contract gap |
| stream events/state | state transition と event を検証・通知 | state を変更し、一部 event を通知 | lifecycle 差分 |

公式 `MediaSourceUT_Common::ValidateInterfaces` は activated source に対して
`IMFMediaSourceEx`、`IMFMediaEventGenerator`、`IMFGetService`、`IKsControl`、
`IMFSampleAllocatorControl` を QueryInterface します。CamBridge の単体 test も同じ
QI matrix を持たせ、直接 activation での不足を検出します。ただし、この test の成功は
別プロセスの Frame Server 互換性を証明しません。

## Lifecycle 差分

公式 sample が検証する主な順序は次の通りです。

1. DLL / class factory / `IMFActivate` の生成
2. `ActivateObject`
3. source `Initialize`
4. source attributes / stream attributes / presentation descriptor
5. allocator usage の取得と、必要なら provided allocator の設定
6. `Start`（presentation descriptor、time format、start position を検証）
7. stream start と source/stream events
8. sample request
9. `Stop`
10. `Shutdown`

現行 CamBridge はこの主要経路を実装していますが、次の差分を確認対象にします。

- `Start` の `pvarStartPos`、stream 数、selected stream の検証が公式より簡略です。
- source の `MENewStream` / `MEUpdatedStream` と stream の state/event の扱いが簡略です。
- stream descriptor 自体に設定する device-stream attributes が不足しています。
- `UsesProvidedAllocator` を返す場合の allocator initialization が公式より簡略です。

これらは contract 差分として記録したものであり、`E_NOINTERFACE` の原因と断定して
いません。まず QueryInterface と Frame Server の lifecycle 到達点をログで確認します。

## COM registration 差分

公式 sample は installer/MSI 経由で machine-wide COM registration を行い、
`HKLM\\Software\\Classes\\CLSID\\<CLSID>\\InProcServer32` に DLL の絶対パスと
`ThreadingModel=Both` を持たせます。

CamBridge manager の `--machine` は同じ InProcServer32 の意味で HKLM に書き込み、
通常経路は HKCU に書き込みます。現行の実機環境で読み取り確認できた値は次の通りです。

| Hive | InprocServer32 default | ThreadingModel |
|---|---|---|
| HKCU | `C:\\Users\\kukeke\\Downloads\\CamBridge\\build\\native-mvp\\Release\\cambridge_media_source.dll` | `Both` |
| HKLM | `C:\\Users\\kukeke\\Downloads\\CamBridge\\build\\native-mvp\\Release\\cambridge_media_source.dll` | `Both` |

これは registry の InProcServer32 部分の一致を示しますが、virtual-camera device
identity の登録や Frame Server がどの hive をロードしたかまでは示しません。HKLM の
変更・削除はこの監査では行っていません。

## DLL dependency audit

Release DLL に対して `dumpbin /DEPENDENTS` を実行した結果、依存先は次の通りでした。

- Windows / Media Foundation: `MFPlat.DLL`, `MFSENSORGROUP.dll`, `SHLWAPI.dll`, `ADVAPI32.dll`, `KERNEL32.dll`
- Visual C++ runtime: `MSVCP140.dll`, `VCRUNTIME140.dll`, `VCRUNTIME140_1.dll`, `api-ms-win-crt-*.dll`
- CamBridge 独自 DLL: なし（frame IPC は DLL に静的リンク）
- Debug runtime: `dumpbin` 出力上はなし

現時点では PATH や working directory に依存する CamBridge 独自 DLL は確認されて
いません。一方、VC++ runtime が対象 Windows に存在しない場合は Frame Server 側の
ロードに影響するため、配布時は Release runtime の導入または DLL 配布方針を別途確定
します。依存 DLL の存在確認は別の診断コマンドで行い、ロード成功とは混同しません。

## 原因仮説と切り分け順

| 仮説 | 内容 | 現在の証拠 | 次の観測 |
|---|---|---|---|
| A | HKCU registration が Frame Server に不十分 | 直接 activation は成功、Start は失敗 | HKLM one-time install 後の比較 |
| B | required COM interface 不足 | source/stream のrequired QIを修正し、Frame Serverログで成功 | optional QI以外は解消 |
| C | Media Foundation source/stream contract 不足 | descriptor attributes testは成功、allocator/lifecycle差分は残る | 管理者配置後のcapture |
| D | Frame Server context の dependency load 失敗 | 独自 DLL 依存はなし、VC runtime は要確認 | DLL load log と process-specific log |

一度に複数の仮説を修正しません。Synthetic Virtual Camera の列挙・frame取得が
成功するまで、WebRTC、signaling、H.264 decoder は開始しません。

## 対応 test matrix

| Test | 対象 | 成功条件 | 証拠の種類 |
|---|---|---|---|
| COM QI matrix | direct activated source/stream | 公式 sample の required IID が成功 | unit/in-process |
| lifecycle log | DLL/class factory/source/stream | Frame Server からの最後の到達点を記録 | process log |
| registry diff | HKCU/HKLM | path/threading model と存在状態を比較 | read-only audit |
| dependency audit | Release DLL | imports と runtime を列挙 | static audit |
| custom media source contract | source reader / sample allocator | descriptor、allocator、sample が通る | integration fixture |
| virtual camera registration | `MFCreateVirtualCamera` / `Start` | Start HRESULT と device enumeration | live Windows |
| synthetic capture | capture probe | CamBridge device open、sample 120+、観測 FPS | live Windows |

## 現在の判定

2026-08-29 時点では、`MFCreateVirtualCamera` と direct COM activation は成功し、
当初の `IMFVirtualCamera::Start` は `E_NOINTERFACE`、video input count は 0 でした。
QI matrix で source の `IMFMediaSource` が `E_NOINTERFACE` になることを確認し、
source/stream の継承 interface を WRL の公開 interface 集合へ追加しました。
その後、同じ QI matrix と descriptor attribute test は成功しています。

絶対パス登録の修正後、Frame Server 側の PID別ログで次を確認できました。

- DLL/class factory/activation/load が成功
- source `IMFMediaSourceEx`、`IMFMediaSource`、`IMFMediaEventGenerator`、`IMFGetService`、`IKsControl`、`IMFSampleAllocatorControl` の QI が成功
- `Initialize`、`GetSourceAttributes`、`GetStreamAttributes`、`CreatePresentationDescriptor`、allocator usage、`Start`、`Stop`、`Shutdown` に到達
- `IMFCollection` (`{5BC8A76B-869A-46A3-9B03-FA218A66AEBE}`) の任意の QI は `E_NOINTERFACE`。これは公式 SimpleMediaSource の実装一覧にもないため、現時点では required interface failure と扱わない

したがって、元の `E_NOINTERFACE` の一次原因は source の required COM interface 集合不足として特定できました。
現在の標準ユーザー CurrentUser 試行は、identity property の `E_ACCESSDENIED` に続いて
`IMFVirtualCamera::Start` も `E_ACCESSDENIED` となります。これは一度だけの HKLM/管理者
installation を実行して検証する別ゲートであり、Synthetic Virtual Camera はまだ未合格です。

### 追加で確認した登録上の不具合

manager に相対 `--source` path を渡すと、COM registry に相対値が書かれ、同一プロセスの
直接 `LoadLibrary` は成功しても COM activation が `0x8007007E` になることを再現しました。
manager は現在 `GetFullPathNameW` で絶対パスへ正規化してから HKCU/HKLM に書き込みます。

### 最新 dependency 結果

Release DLL の `dumpbin /DEPENDENTS` は `MFPlat.DLL`、`MFSENSORGROUP.dll`、`SHLWAPI.dll`、
`SHELL32.dll`、`ole32.dll`、`ADVAPI32.dll`、`KERNEL32.dll` と VC runtime (`MSVCP140.dll`、
`VCRUNTIME140*.dll`、`api-ms-win-crt-*.dll`) を示しました。CamBridge 独自 DLL と Debug
runtime への依存はありません。`SHELL32.dll` は診断ログのディレクトリ作成に使用する
Windows標準DLLです。Release VC runtime がない環境でのロード可否は未実機確認です。

### Program Files 配置検証ゲート

次の実機検証では、build directory の DLL を直接登録せず、
`C:\Program Files\CamBridge\Native\cambridge_media_source.dll` を HKLM の
`InprocServer32` に登録します。installer は標準の Program Files ACL を継承し、
Everyone Full Control のような追加ACLは設定しません。DLLとコピー元artifactの
`Zone.Identifier`、`icacls`、SYSTEM/Local Service の直接ACE有無を出力します。

Local Service/SYSTEMとしての実ファイルopenは、通常ユーザーのこの開発環境では
実行していません。直接ACEがない場合も、Users等の継承・token groupを含む実効権限の
証明にはならないため、管理者インストール後のFrame ServerログとStart結果を一次証拠にします。

現行 build DLL の読み取り監査は、`Zone.Identifier: none` で、ACLは SYSTEM と
Administrators が Full、現在ユーザーが Full、sandbox users が Read/Execute でした。
Local Service の直接ACEはありません。`C:\Program Files\CamBridge\Native` はまだ
作成していないため、Program Files版のACL差分とStart結果は未確認です。

installer launcher:

- `Install-CamBridge-Native.cmd`: copy、ACL/MOTW audit、HKLM registration、Start、synthetic capture probe
- `Uninstall-CamBridge-Native.cmd`: CurrentUser Virtual Camera Remove、HKLM COM removal、exact install directory cleanup

以上により、source contract と registration path の直接問題は修正・検証済みですが、
CurrentUserの権限境界を越えた実機登録と、capture clientからの実フレーム取得は未完了です。

### Identity property を除外した再試行

`AddProperty` が Start の一次原因かを分離するため、custom identity property を設定しない
manager経路でも再試行しました。結果は `MFCreateVirtualCamera(CurrentUser): success`、
`IMFVirtualCamera::Start: 0x80070005 (E_ACCESSDENIED)`、`Video input count: 0` でした。
したがって、`AddProperty` の `E_ACCESSDENIED` だけが Start の原因ではありません。
Native MVPの既定install launcherではidentity propertyをスキップし、Program Files配置と
HKLM registrationを先に検証します。`--identity-properties` は明示診断用に残しています。
