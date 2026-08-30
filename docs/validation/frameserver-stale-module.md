# Frame Server stale-module handling note

## Finding

Updating the registered CamBridge Media Source DLL does not update a module
already loaded by a running `FrameServer` process. The previous zero-sample
observation was made with an older timestamped DLL still resident in the old
Frame Server process. A safe Frame Server restart caused the new process to
load the current registered DLL, after which the existing event, IPC, sample,
and SourceReader path delivered 120 samples.

## Detection

When a Media Source binary is rebuilt, record all of the following before
testing:

- the absolute path registered in `HKLM\Software\Classes\CLSID\...\InprocServer32`;
- the SHA-256 of that installed file;
- the Frame Server PID and start time;
- the CamBridge module path and file identity visible in that process, when
  module inspection is permitted;
- the PID-scoped Media Source diagnostic log created by that module.

The installed Program Files artifact is the authority for a machine test. A
different hash in a local build directory only proves that a newer build has
not yet been installed.

## Restart policy

The current installer does not automatically kill or restart Frame Server.
That is intentional: a restart can interrupt another application using a
camera and forced termination would be unsafe. If the registered DLL hash is
newer than the module identity in the active Frame Server process, the operator
must close camera clients and perform a documented, safe restart before the
acceptance probe. The restart result and new PID must be recorded with the
probe evidence.

Before adding any automatic restart, measure its effect on active camera
clients, define a user-confirmed maintenance action, and provide rollback. No
such automatic behavior is part of the Native MVP.

## Regression rule

Do not diagnose allocator, event-contract, or IPC failure from a zero-sample
run until the loaded module identity has been correlated with the registered
artifact. Keep the bounded 120-sample Synthetic Publisher → Virtual Camera →
SourceReader test as the regression baseline.
