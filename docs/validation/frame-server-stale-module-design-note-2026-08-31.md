# Frame Server stale module handling design note

## Problem

Windows Camera Frame Server components can outlive a CamBridge installer or diagnostic run. A long-lived Frame Server process may retain an older copy of `cambridge_media_source.dll` even after the COM registration and installed file have changed. In that state, a new capture probe can exercise code that is not the code currently on disk.

The incident observed on 2026-08-31 demonstrated this: the old PID produced the historical `RequestSample=0` result, while a safe Frame Server restart created PID `33492` and the current DLL then delivered 120 samples.

## Detection evidence

Before interpreting a Media Source failure, collect all of the following:

1. Registered COM `InProcServer32` absolute path.
2. SHA-256 of the registered DLL and the build artifact.
3. Frame Server/FrameServerMonitor PID and process start time.
4. Media Source diagnostic log PID and DLL-load timestamp.
5. If available, the loaded module path and file identity for that PID.
6. A bounded capture probe result correlated to that PID.

The diagnostic log must identify the DLL-load PID. A run is not considered a current-build regression until the diagnostic PID and installed DLL identity are correlated.

The post-restart bounded repeat also confirmed the recovery state: with the
installed Program Files artifact in place, a finite synthetic publisher and the
capture probe completed with `Samples received: 120`, SourceReader type
`NV12 1920x1080 60/1`, and clean source shutdown. No new stale-module symptom
was observed during that repeat.

## When a restart is needed

A safe Frame Server restart is needed when the registered DLL or its binary identity changed while the existing Frame Server process is still alive and the process has not demonstrably loaded the new identity. It is also needed when the log PID predates the installed artifact change.

No restart is needed merely because a probe returned an initial `MF_SOURCE_READERF_STREAMTICK`; the current implementation can return that tick before the first sample.

## Restart policy

The current product policy is manual and safe, not automatic:

- Do not add a forced `taskkill` or service restart to the installer.
- Do not terminate a Frame Server process while a user camera client may be active.
- Ask the user to close CamBridge and camera-consuming applications, then perform the documented safe restart procedure.
- Re-run the bounded synthetic probe and inspect the new PID before changing Media Source code.

This avoids disrupting Discord, Zoom, browsers, or other camera clients and keeps an observed stale-module condition separate from a source-contract defect.

## Future optional automation

An automatic restart can be considered only after the user experience and Windows camera-client impact are characterized. Any future automation must:

- detect active camera consumers;
- show or log the reason and affected process;
- wait for a safe point rather than force termination;
- preserve a manual recovery path;
- record old/new PID and module identity;
- fall back to a no-restart diagnostic report when the camera is in use.

This note intentionally does not implement that automation.

## Rollback

If a future diagnostic or installer change regresses the gate, restore the previous CamBridge commit and repeat the safe PID-correlated probe. Do not delete registry state or terminate unrelated processes as a rollback mechanism.
