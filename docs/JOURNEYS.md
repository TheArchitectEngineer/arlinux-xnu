# Real iOS application journeys

The sole entry is build.sh build --mode app (see README for workstation setup).
App mode requires the shared Darling session and runs the entire manifest,
journeys/ios-apps.json. The runner builds original ARM64 Mach-O applications
against declaration-only interfaces under guests/sdk; UIKit/Foundation/libSystem
implementations come from the pinned real iOS userland. There is no test backend,
fixture action RPC, injected click handler or replacement framework.

## Apps and effects

ARHello 1.0 is a counter. ARWorkbench 1.0 edits/saves a note and exposes genuine
thread, 16 KiB memory, child-process and XPC operations through UIKit controls.
Its bundled ARJourneyWorker uses Foundation NSXPCListener; the app requests it
with NSXPCConnection. The suite never starts the helper on the app's behalf.

| Journey | Required externally observed effects |
| --- | --- |
| counter-lifecycle | Visible Count: 0; real click -> Count: 1; normal close/status 0; new process -> Count: 0; normal close |
| text-file-persistence | Real field click and keyboard input; Save -> displayed saved text; normal close; new process reads/displays the same saved text |
| thread-memory | Real buttons trigger pthread create/join with shared value/return result, then read/write/unmap a 16 KiB region; their results appear in the UI |
| app-child | App requests a child, observes wait/exit 37 and displays it; normal close |
| xpc-helper-lifecycle | App requests bundled helper; genuine Foundation reply -> displayed pong; independently running service process in the same session; helper retires after close; app/helper restart with new identities |
| concurrent-relaunch | Two independent app binaries run in one session; close/restart workbench while the original counter keeps its identity and remains clickable |
| crash-isolation | Kill workbench as a deliberate crash; shared session and original counter survive and respond to another click; fresh workbench can launch and close |

A visible action label is a prerequisite, not the effect assertion. A click only
passes its input operation; the following new compositor-visible value must pass
separately. Helper success requires both the real reply and independent process
provenance. App stdout is diagnostic evidence, never the visual oracle.

## Real input and real composition

An Xvfb X server hosts Weston's X11 backend, desktop shell and genuine Wayland
seat. This supplies actual pointer/keyboard/focus/close events on a hosted runner.
scrot captures the composed X display, including the visible Weston surfaces;
tesseract records its OCR TSV and text. Control clicks use unique observed word
bounds, not hardcoded app coordinates. Ambiguous or missing controls fail.
Screenshot PNGs are never edited to satisfy an assertion. Renderer buffers are
additional diagnostics, not substituted for the composed screenshot.

This is a real nested desktop, not a phone. Android acceptance must separately
exercise the same app journeys through ARLinux's actual device input/display.
The PlayCover/macOS journey remains a separate Apple-host reference;
it does not currently execute this entire manifest and cannot certify Linux.

## Lifecycle and evidence

Each case starts with a new state directory. Relaunch within that case preserves
only its own files. App PID/start ticks/executable and binary SHA-256 are recorded
per generation. All cases use the actual shared session identity. Existing app
processes are never silently replaced. Helpers must match their own guest binary,
explicit service role and this session, with a distinct process identity.

Every manifest step gets passed/failed/blocked status. The first required failure
blocks dependent steps; later cases still run. Overall success requires every
case to pass. Infrastructure errors, app crashes, missing pixels and unsupported
input are failures. Failure cleanup is reported separately and never counts as
normal exit. The suite does not suppress or xfail current compatibility gaps.

Evidence is under build/dyld/journeys: result.json, composed PNGs with SHA-256,
OCR TSV/text, original runtime/stdout/stderr, original-process LLDB snapshots and
same-cache symbolication. One failing app is not replayed in a new process and
called equivalent. The normal experiment receipt hashes these artifacts and
pins the source/run/attempt and Apple inputs. build.sh report prints IOS_SUITE
and each IOS_JOURNEY result; download selects the existing dyld evidence artifact.

## Limits that must remain visible

This is a complete execution-to-observation pipeline for the listed journeys,
not exhaustive iOS API coverage or proof that all listed journeys currently pass.
The first hosted run (39442480042) failed all six then-defined cases on missing
controls; two apps were visibly rendered. XPC was added as a seventh required
journey afterward. Blocked steps have not exercised their effects. In particular,
full Mach-right/audit cleanup is not independently established merely by checking
that the provider PID stays alive. Extend observable service lifecycle evidence
and add real app journeys when implementing those remaining semantics.

The Apple-host reference at run [39446975446](https://github.com/taowen/arlinux-xnu/actions/runs/39446975446)
(source 090b0d5629317872f36af010b09bd7b3e6716549) passed two real click -> Count: 1
-> normal exit/status 0 cycles, with verified screenshots and original LLDB logs.
This calibrates ARHello behavior, not all seven journey cases. Linux run
[39448373812](https://github.com/taowen/arlinux-xnu/actions/runs/39448373812)
(source 7296e4ce842dbb1f0d4f0a33cd8009c0a3bba7c7) fails all seven cases on missing
visible controls. Writable app containers, helper launch and complete input/close
semantics remain implementation gaps, not completed effects hidden by the suite.
