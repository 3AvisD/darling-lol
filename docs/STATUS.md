# Snapshot status — 2026-10-13

This repository preserves a development runtime, including historical experiments. All 29 patches apply to their pinned revisions and the exported source matches the development tree for the patched files. A fresh checkout/build/install has not been validated end to end on another machine. The normal workflow uses manual client installation and play, without debugger attachment, client API calls or input automation.

## New PC: verified runtime changes

The current machine uses a Ryzen 5 5500, RX 6600 and Linux 7.2.9. The source snapshot includes the build corrections needed with its newer Clang/LLVM: x86-64-only OpenDirectory, consistent objc4 debug macros, older Objective-C collection code generation and LLVM 22/23 branch opcode compatibility.

- **Video scaling:** generated ELF GL resolvers lost floating-point arguments on the first call. Chromium's first linear sampler request failed, leaving nearest sampling. Preserving registers fixes the native first-call sampler/color probe; the old wrapper fails both checks. An 800×450 checkerboard scaled to 640×360 changes to bilinear sampling.
- **Startup presentation:** drawing another differently sized surface in the same GL context stopped the active async presenter. Those surfaces now fall back to readback without stopping it. The old build fails after three visible frames; the corrected native probe retains the presenter through 357 fallback frames and presents all 360 frames at about 60 FPS. User confirmation of the splash remains pending.
- **UI clicks:** window dragging now examines the frontmost view by geometry, including Electron's control-region overlays that deliberately return nil from hit testing. Previously a click could reach the underlying draggable web view and start a drag instead of activating a control.
- **External-link crashes:** six LaunchServices paths dereferenced nullable error-output pointers. They now check the pointers and populate supplied outputs. AppKit also implements the missing URL-opening configuration/completion methods. Native nullable-output checks passed. The paired installer covers Electron's absolute LaunchServices subframework path as well as the flat override.
- **Installation:** the default prefix gets a writable `/private/tmp`; the first-time launcher skips the inaccessible native intro and avoids an installed-client `--app-root`. The fetcher accepts missing informational `Info.plist` metadata. A separate HTTP 409 LaunchAgent handoff failure remains unresolved in the manual launch path.

The historical update hook handles HTTP 409, interrupted `pending_verify` and active downloads in 28 mocked scenarios. Historical external-link forwarding was exercised before the workflow changed. Both inspector-based helpers are now disabled in normal launches. Native URL methods can queue requests, but no normal host spool watcher consumes them; host-browser forwarding is not established by the crash fix. Legacy Electron visibility patches and automated practice/profiling tools are excluded from recommended setup/testing.

## FPS observation and server optimization

An 18-minute host observation found substantial Darling request overhead and repeated main-thread socket waits consistent with its RPC path. The user reported 100–120 FPS in quieter moments and 70–80 during fights, versus 170–190 on Windows. GPU utilization averaged about 61% with an 85% maximum. CPU temperatures were 92–94°C, with reported clocks averaging 4.13 GHz. Background clients remained busy; the server still used about 1.5 logical CPUs after the observed game exited.

Server-only CPU samples identified worker context switching and receive/message allocation as substantial costs. These observations do not establish the entire FPS gap, distinguish every intentional game wait from added RPC latency, or prove a per-frame cause.

The receive queue now retains unused receive slots, transfers real messages into the existing queue and adopts every returned control header before potentially throwing. This preserves received descriptor ownership even if deque allocation fails. Disabled debug logging no longer prepares PID/TID strings. Independent host socket checks reduce warmed empty receive allocations from 32 to zero and single-packet allocations from 64 to two. Normal and address/undefined-behavior sanitizer checks passed, including exception cleanup and descriptor ownership.

The normal launcher no longer enables a client inspector, starts scheduling/update/link helpers or forces unlimited UI frames. The user installed the new server atomically while the old server remained active. **No post-activation FPS gain has been measured.** Validate through manually played matches after a full Darling restart when the game and clients are closed.

## Memory reclamation and activation

A memory audit attributed roughly 4.3 GiB of proportional resident memory to the clients/runtime, with 50 crash-handler processes present. About 3 GiB was private anonymous memory/native heap, and graphics mappings accounted for about 238 MiB. A five-minute idle observation showed no rapid growth (Riot changed about 1 MiB, League about 10 MiB). All observed processes reported zero `LazyFree` memory.

Installed-library disassembly confirmed that Darwin `MADV_FREE`, `MADV_FREE_REUSABLE` and `MADV_FREE_REUSE` were successful no-ops. Apple malloc uses the free/reusable hints for freed page ranges. The patch maps free/reusable to Linux `MADV_FREE` and reuse to `MADV_POPULATE_WRITE`, cancelling lazy reclamation without changing surviving contents. Linux 5.14+ is required.

The kernel library built; host and independent guest probes marked all 4 MiB of their own buffer reclaimable and cancelled that state on reuse while preserving contents. The old implementation marked zero pages. Exported symbols and library dependencies remained unchanged. This establishes the reclamation fix, not the fraction of the clients' footprint it will recover. Lazy reclamation may leave RSS unchanged while RAM is available.

The memory installer initially failed because the sudo server installation had made `build/backups` root-owned. Its default is now user-owned `build/memory-runtime-backups`, and it stages every replacement before changing any destination. The corrected installer replaced the runtime library, per-prefix bootstrap copy and prepared artifact without stopping processes; all 75 existing process mappings retained their old inode.

A fresh standalone guest probe still saw the old implementation: the active overlay exposed the old library despite the host file having the new checksum. **The fixes are installed on disk and require a full namespace restart to activate.** After manually exiting the game and both clients, run `darling shutdown`, then `sh scripts/run.sh`. Neither installer performs that restart. Real-client RAM improvement remains unmeasured. See [setup](SETUP.md) and [troubleshooting](TROUBLESHOOTING.md).

## Earlier laptop results (historical)

The earlier AMD Barcelo laptop used RADV, Xwayland/Hyprland and two 60 Hz displays without active VRR. Results from its historical automated practice experiments are not current validation and those workflows are no longer recommended.

Confirmed earlier fixes include a lost wake in kernel workqueue semaphore handling, callback-safe CoreAudio stop, AppKit event handling/post-game controls and delivery of ready main-queue callbacks on every event poll while throttling expensive CF run-loop work. The presentation callback regression improved from 180 callbacks during 200 polls to 200 with zero late polls. Native pixel and input checks passed.

A deferred compositor path used to export one GPU synchronization FD per frame and lose ownership. Scoped ownership reduced a 120-frame native probe from 120 leaked FDs to zero; extended compositor probes also remained flat. A later practice capture had higher GPU utilization and flatter presentation intervals, without isolating a clean before/after performance comparison.

Historical short Baron runs measured 99.6 FPS average / 16.11 ms p99 and 85.7 FPS average / 23.51 ms p99 in a spell-heavy attempt. Refresh/camera differences and old reset helpers make those comparisons unreliable. They do not demonstrate a 90 FPS p99 result or an ARAM guarantee. Laptop-specific CPU affinity is not installed automatically; do not use `pin-game.sh` unchanged on another CPU.
