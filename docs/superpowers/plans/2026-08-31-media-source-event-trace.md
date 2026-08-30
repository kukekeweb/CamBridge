# Media Source Event Contract Trace Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 診断ビルドでCamBridge Media SourceのStart後イベント、イベント取得、Presentation Descriptor、RequestSample到達状況を記録し、Frame Server経路の最初の分岐点を特定できるようにする。

**Architecture:** 既存のcontrol-path loggerを拡張し、QueueEventの種類・status・return HRESULT・payload、BeginGetEvent/EndGetEvent/GetEvent、Start時のstream selectionとmedia type、RequestSample countersをPID単位のログへ出力する。Direct Media Source testは同じログ形式を使い、既存のSourceReader/Frame Server経路とは別の証拠として扱う。修正対象は診断とfixtureに限定し、sample生成・IPC・allocator・registration・installer・WebRTCは変更しない。

**Tech Stack:** C++20, Windows Media Foundation, COM/WRL, CMake/CTest, PowerShell log analysis.

**Spec:** 現在のユーザー要求「Start後イベントの完全trace、event取得trace、PresentationDescriptor/stream selection確認、RequestSample=0の直接証明、STREAMTICK発生源確認、direct/SourceReader/Frame Server比較」。

## Global Constraints

- WebRTC sender/receiver/decoderは実装しない。
- allocator、sample buffer生成、IPC format/mapping、publisher、SourceReader設定、media type、Virtual Camera registration、installerは変更しない。
- frameごとの大量ログは追加せず、COM activation、event、state、sample要求などのcontrol pathだけを記録する。
- QueryInterfaceの成功ログは抑制可能にし、失敗HRESULTは保持する。
- Frame Serverの実機ログとdirect fixtureのログを別PID・別経路として判定する。
- 原因確定前に症状回避のsample pushやtimer駆動を追加しない。

### Task 1: Event trace API RED test

**Files:**
- Create: `windows/native-mvp/tests/diagnostic_event_trace_tests.cpp`
- Modify: `windows/native-mvp/CMakeLists.txt`

**Interfaces:**
- Consumes: `cambridge::native::MediaEventTypeName(DWORD)`.
- Produces: CTestで主要Media Foundation eventのsymbolic name mappingを検証する。

- [x] **Step 1: Write the failing test**

`MENewStream`, `MEUpdatedStream`, `MESourceStarted`, `MEStreamStarted`, `MEMediaSample`, `MEError`の名前を新しい診断APIへ要求する。

- [x] **Step 2: Run test to verify it fails**

Run: `cmake --build build/native-mvp --config Release --target cambridge_diagnostic_event_trace_tests`

Expected: `MediaEventTypeName`が未定義でコンパイル失敗。

- [x] **Step 3: Implement the minimal mapping API**

`diagnostic_log.h/.cpp`へ`MediaEventTypeName(DWORD)`を追加し、既知のeventはsymbolic name、それ以外は`Unknown`を返す。

- [x] **Step 4: Run test to verify it passes**

Run: `ctest --test-dir build/native-mvp -C Release -R cambridge_diagnostic_event_trace_tests --output-on-failure`

Expected: PASS。

- [ ] **Step 5: Commit**

`git commit -am "test: add media event trace contract checks"`

### Task 2: Queue/GetEvent and descriptor diagnostics

**Files:**
- Modify: `windows/native-mvp/virtual-camera/diagnostic_log.h`
- Modify: `windows/native-mvp/virtual-camera/diagnostic_log.cpp`
- Modify: `windows/native-mvp/virtual-camera/cambridge_media_source.h`
- Modify: `windows/native-mvp/virtual-camera/cambridge_media_source.cpp`

**Interfaces:**
- Consumes: `MediaEventTypeName`, existing `LogControlEvent` and `LogQueryInterface`.
- Produces: `event=QueueEvent`, `event=BeginGetEvent`, `event=EndGetEvent`, `event=GetEvent`, `event=Start.descriptor` log records containing event type, numeric type, status, call HRESULT, extended GUID, associated-object flag, stream id, and sequence where applicable.

- [x] **Step 1: Add failing assertions to the direct event fixture**
- [x] **Step 2: Run direct fixture and confirm new event fields are absent**
- [x] **Step 3: Add logging helpers and event inspection**
- [x] **Step 4: Log descriptor count, stream id, selected flag, major/subtype, dimensions, and FPS at Source::Start**
- [x] **Step 5: Suppress successful high-volume Source/Stream QI by default while retaining failures and an opt-in verbose environment switch**
- [x] **Step 6: Run direct fixture and verify event trace records**

### Task 3: RequestSample counters and STREAMTICK evidence

**Files:**
- Modify: `windows/native-mvp/virtual-camera/cambridge_media_source.h`
- Modify: `windows/native-mvp/virtual-camera/cambridge_media_source.cpp`
- Modify: `windows/native-mvp/virtual-camera/diagnostic_log.h`
- Modify: `windows/native-mvp/virtual-camera/diagnostic_log.cpp`

**Interfaces:**
- Consumes: existing `RequestSample`, `CreateSample`, `MEMediaSample` flow.
- Produces: bounded RequestSample entry/success/failure counters, first-request wall-clock timestamp, and shutdown summary. Logs explicitly show whether CamBridge queues `MEStreamTick` (expected zero in current implementation).

- [x] **Step 1: Add a failing direct-fixture assertion for RequestSample summary fields**
- [x] **Step 2: Run the fixture and confirm the summary fields are absent**
- [x] **Step 3: Add counters for every RequestSample return path and log the summary at Shutdown**
- [x] **Step 4: Audit all MediaEvent queue calls and record that no `MEStreamTick` is queued by CamBridge**
- [x] **Step 5: Run direct and existing CTest suites**

### Task 4: Evidence collection and self-review

**Files:**
- Modify: `windows/native-mvp/tests/media_source_tests.cpp` only if required to print event-contract evidence.
- Create: `docs/validation/media-source-event-trace-2026-08-31.md`

**Interfaces:**
- Consumes: direct fixture logs, latest Frame Server log, latest capture child log.
- Produces: a report separating direct, SourceReader, and Frame Server timelines and classifying the result as A-H without claiming a root cause until evidence supports it.

- [x] **Step 1: Run CMake/CTest and direct event fixture**
- [ ] **Step 2: Run bounded synthetic capture probe with the newly installed diagnostic DLL**
- [x] **Step 3: Extract the latest PID-correlated pre-instrumentation Frame Server trace**
- [x] **Step 4: Compare first divergent event/call and STREAMTICK source evidence**
- [x] **Step 5: Record unresolved alternatives and explicitly stop before any behavioral fix**

## Self-review checklist

- Spec coverage: QueueEvent, event retrieval, descriptor selection, RequestSample counter, STREAMTICK audit, direct fixture, and A-H classification are covered by Tasks 2-4.
- No-placeholder check: each task names exact files, commands, and observable outputs; no production fix is implied.
- Type consistency: `MediaEventTypeName(DWORD)` is declared in `diagnostic_log.h`, implemented in `diagnostic_log.cpp`, and consumed by the diagnostic test and event logger.
