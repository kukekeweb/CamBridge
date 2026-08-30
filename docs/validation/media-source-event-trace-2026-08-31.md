# Media Source event contract trace (2026-08-31)

## Scope

This document records the diagnostic-only change for the Synthetic Virtual Camera boundary. WebRTC, allocator behavior, IPC layout, publisher, registration, installer, and media type selection were not changed.

## Diagnostic fields

The Media Source DLL now records control-path `QueueEvent`, `BeginGetEvent`, `EndGetEvent`, and `GetEvent` operations with:

- symbolic and numeric `MediaEventType`
- Queue/Get call HRESULT
- event status HRESULT
- extended type GUID
- associated-object presence and pointer
- stream id
- sequence where applicable
- descriptor count, selection, major/subtype, dimensions, and FPS at `Start`
- `RequestSample` total, success, failure, first-request FILETIME, produced, and delivered counts at shutdown

Successful high-volume `QueryInterface` records for the source/stream objects are suppressed by default. Failures remain logged. Set `CAMBRIDGE_NATIVE_MVP_VERBOSE_QI=1` to restore successful QI records for a diagnostic run.

CamBridge does not queue `MEStreamTick`; each source initialization records `EventAudit.NotQueued` as explicit evidence.

## Direct fixture result

The direct Media Source fixture was run with the newly built DLL and an isolated log directory:

```text
Media Stream sample test: time=304205473108 duration=166666 bytes=3110400
Media Source test: types=3 start=0x0
fixtureExit=0
```

Observed order:

```text
Start.descriptor selected=1 majorType=video subtype=NV12 width=1920 height=1080 fps=60/1
QueueEvent MENewStream callHr=S_OK status=S_OK associatedObject=1 streamId=0
QueueEvent MEStreamStarted callHr=S_OK status=S_OK associatedObject=0 streamId=0
QueueEvent MESourceStarted callHr=S_OK status=S_OK associatedObject=0 streamId=0
GetEvent MENewStream callHr=S_OK status=S_OK associatedObject=1 streamId=0
GetEvent MEStreamStarted callHr=S_OK status=S_OK associatedObject=0 streamId=0
QueueEvent MEMediaSample callHr=S_OK status=S_OK associatedObject=1 sequence=0
GetEvent MEMediaSample callHr=S_OK status=S_OK associatedObject=1 sequence=0
QueueEvent MEStreamStopped callHr=S_OK status=S_OK
QueueEvent MESourceStopped callHr=S_OK status=S_OK
Shutdown.request-summary requestSamples=1 requestSuccesses=1 requestFailures=0 samplesProduced=1 samplesDelivered=1
```

This is a direct fixture. It explicitly calls `RequestSample` and therefore does not prove that Frame Server will issue the request.

## Historical Frame Server evidence before the safe Frame Server restart

The earlier 21:46 run used a Frame Server process that had retained an older timestamped Media Source DLL and produced this boundary:

```text
Media Source Start: S_OK
Stream Start: S_OK
Start format: NV12 1920x1080@60
RequestSample: 0 observed records
SampleCreated: 0 observed records
SampleDelivered: 0 observed records
```

The matching capture child returned `S_OK` with `MF_SOURCE_READERF_STREAMTICK` and `sample=no`. This was valid evidence for that old process, but not for the newly built DLL.

## Final Frame Server evidence after the safe restart

The safe Frame Server restart created PID `33492`, which loaded the current diagnostic DLL. The installed and build DLLs matched at SHA-256:

```text
2A6F5D7182F3F9C18B13546E6C9D7B97E14688B72ADF927B9D0D8FC8ABF2925E
```

The capture child used the CamBridge device, selected `NV12 1920x1080 60/1`, and received 120 samples with exit code 0. The first `ReadSample` returned the expected stream tick with no sample; the following reads returned samples. A stream tick is not a CamBridge-queued event in this implementation (`EventAudit.NotQueued`), and it did not prevent delivery.

Correlated event sequence from `C:\ProgramData\CamBridge\logs\media-source-33492.log`:

```text
Start.descriptor selected=1 streamId=0 subtype=NV12 width=1920 height=1080 fps=60/1
QueueEvent MENewStream callHr=S_OK status=S_OK associatedObject=1
QueueEvent MEStreamStarted callHr=S_OK status=S_OK
QueueEvent MESourceStarted callHr=S_OK status=S_OK
EndGetEvent MENewStream S_OK
EndGetEvent MEStreamStarted S_OK
EndGetEvent MESourceStarted S_OK
RequestSample.begin
CreateSample.ipc / SampleCreated S_OK
QueueEvent MEMediaSample callHr=S_OK status=S_OK
EndGetEvent MEMediaSample S_OK
RequestSample ...
...
SourceReader: 120 samples, exit=0
```

The first actual capture session therefore crossed the previously failing boundary. The old `RequestSample=0` was caused by a stale Frame Server process retaining the old DLL, not by the current Media Source event contract. The initial no-publisher enumeration session in the same log has a separate shutdown summary with `requestSamples=0`; it must not be combined with the later capture session.

## Final classification

**Root cause: stale Frame Server module retention / process lifecycle.** The old Frame Server PID continued using the previous timestamped DLL after the registered/build artifact changed. After a safe restart, the new PID loaded the diagnostic DLL and the full event-to-sample path succeeded.

This is not classified as A-F for the current build: the current event queue, event consumption, descriptor selection, RequestSample, sample creation, and delivery all succeed. The historical pre-restart observation was H only until the PID-correlated run was available.

## Native synthetic gate

The current real-machine gate is **PASS** for the bounded synthetic capture contract:

- CamBridge device enumeration: successful
- SourceReader media type: NV12, 1920x1080, 60/1
- First stream tick: observed, no sample, expected
- Samples received: 120
- Capture child exit: 0
- Media Source shutdown: `S_OK` in the capture child
- Residual synthetic publisher/capture probe processes: none observed

This gate does not claim a 10-minute hardware or WebRTC result. It is the isolated Synthetic Publisher → IPC/Media Source → Virtual Camera → SourceReader regression gate.
