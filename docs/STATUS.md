# Snapshot status — 2026-10-14

This repository preserves a development runtime, including historical experiments. All 29 patches apply to their pinned revisions and the exported source matches the development tree for the patched files. A fresh checkout/build/install has not been validated end to end on another machine. The normal workflow uses manual client installation and play, without debugger attachment, client API calls or input automation.

## Game FPS, 2026-10-13/14

All matches were ARAM, played manually with unchanged game settings; FPS is the game's own 10-second average (`netstats.csv`). Bilgewater runs a few percent below Hexgate on the same build.

| Match | Map | Runtime | Avg FPS | Slowest quarter |
|---|---|---|---|---|
| 10-13 12:51 | Hexgate | before this work | 83.8 | — |
| 10-13 13:14 | Bilgewater | before this work | 76.9 | — |
| 10-13 16:54 | Hexgate | first kernel batch | 110.7 | — |
| 10-13 20:36 | Bilgewater | + `-O2` libc/libc++, AVX2 string primitives, real CPU count | 109.7 | — |
| 10-14 13:04 | Hexgate | lock switches off (lost to a reboot) | 119.2 | 104.2 |
| 10-14 14:18 | Hexgate | + core isolation (since made opt-in) | 102.9 | 92.0 |
| 10-14 14:42 | Bilgewater | local mutexes, condition variables and semaphores | 119.5 | 103.4 |
| 10-14 16:26 | Hexgate | + Metal bind deduplication and cheaper blits | 126.7 | 109.6 |
| 10-14 20:07 | Hexgate | + Indium hand-off changes (since made opt-in) | 114.8 | 102.9 |
| 10-14 20:27 | Bilgewater | same | 107.2 | 97.2 |
| 10-14 23:19 | Hexgate | hand-off removed; + vertex-descriptor dirty masks | 118.5 | 106.0 |
| 10-14 23:48 | Hexgate | same, Hyprland `render:direct_scanout = 0` | 135.4 | 118.7 |

The 13:04 and 16:26 Hexgate matches were captured with the passive sampler (`tools/riot-match-capture`). Per frame, the game's main thread waited 1.36 ms on darlingserver in the first and 0.10 ms in the second. It blocked 39 times per frame in the first and 14 in the second. darlingserver CPU fell from 82% to 61% of a core. GPU utilization stayed at 70–74%: the main thread is the limit and is now about 90% busy running code. Frame times within each 10-second window vary by about 1–1.4 ms (standard deviation) and never more than 2.7 ms, versus hitch windows of 77–168 ms in the matches of 10-13 morning.

The Indium hand-off round moved command-buffer setup, blits and the frame-end synchronisation onto Indium's submit thread. In both of its matches the main thread waited on futexes 0.77–0.79 ms per frame instead of about 0.5 ms, and FPS fell below every earlier match on the same map, so those changes are now opt-in (see [setup](SETUP.md#metal-and-indium-overlays)).

The 23:48 match ran the same Darling build as 23:19, with Hyprland's direct scanout turned off to fix a cursor that stopped changing shape in fullscreen (see [troubleshooting](TROUBLESHOOTING.md#cursor-stops-changing-shape-in-fullscreen)). Over the first ten minutes of each match, average FPS rose from 120.6 to 136.3 and the slowest quarter from 107.3 to 118.8; main-thread CPU per frame fell from 7.1–7.7 ms to 6.5 ms. That match lasted 11 minutes, so one more match is needed before crediting the whole gain to the compositor setting.

What changed in the runtime (all in the patches):

- **Process-local pthread mutexes and condition variables** (`psynch_mutexwait.c`, `bsdthread_register.c`): contended first-fit mutexes and their condition variables sleep on futexes inside the game instead of making two darlingserver round trips. On by default in the League game process only.
- **Process-local Mach semaphores** (`mach_traps.c`): semaphores the game creates are counted and waited on in the process. A destroyed or reused slot can no longer satisfy a wait or take another semaphore's signal: the count and the slot generation change in one compare-exchange, and a destroy leaves a negative marker. A forked child resets the table, and every name the table did not create goes to the server. The game's worker threads wake 380 times a second instead of 940. On by default in the game process only. The 2026-10-10 client crash happened while the switch was on in every process; the clients still use the server path. A local wait cannot be interrupted by `thread_abort`, and a semaphore shared with another task is not supported locally.
- **Metal render encoder** (`MTLRenderCommandEncoder.mm`): re-binding the identical texture, sampler, buffer and offset, pipeline or depth-stencil state at the same slot is dropped before any work. The comparison key is the Indium object, which the encoder's binding tables keep alive, so a reused wrapper address cannot alias. The depth-stencil state, which Indium does not retain, is retained by the record. `endEncoding` clears the records. Blit encoder calls pass the resources by reference: 5 atomic operations per copy become 1. `-[MTLVertexDescriptor reset]` no longer resets every attribute twice.
- **Core isolation** (`mach_traps.c`, opt-in with `/tmp/rclol-core-isolation`): gives the game's main thread its own Linux core-scheduling cookie. In its one match the main thread waited for a CPU 12.6% of the time instead of 1%, and FPS fell from 118 to 103 over the same minutes. It stays in the tree for experiments only.

Hardware counters on the main thread (10-14 17:26, Hexgate, 20 minutes, user space only): 0.64 instructions per cycle at about 4.0 GHz, 19.4 L1 data-cache misses, 3.3 branch mispredictions and 0.9 instruction-cache misses per 1000 instructions. Page walks are 1.55 per 1000 instructions for data (95% on 4 KiB pages) and 0.26 for code. Even if every walk stalled the thread for 30 cycles with no overlap, they would cost at most 3.5% of its cycles, and the slowest and fastest quarters of the match look the same. Huge pages for the game's code could therefore recover well under 1%, not enough to justify running the game from copied code; for its heap (27% of anonymous memory was on huge pages in this match), 1–3% at most. The low IPC comes from cache misses inside the game's own data structures. That match averaged 114 FPS (worst 10 seconds 85.7) with the same runtime as the 16:26 one; the difference is the match itself (longer, later-game fights), since the main thread's server waits were unchanged at 0.11 ms per frame.

Where the main thread's time goes now: about 72% of its user time is in the game binary, mostly in memory-latency-bound code. The game binary runs natively and selects its AVX paths with `cpuid`. Objective-C dispatch, the Metal wrapper, Indium, the allocator and the Vulkan driver take most of the rest. The remaining Darling-side levers are smaller: more of the heap on transparent huge pages (35% of anonymous memory is), and the per-draw Metal/Objective-C path.

## New PC: verified runtime changes

The current machine uses a Ryzen 5 5500, RX 6600 and Linux 7.2.9. The source snapshot includes the build corrections needed with its newer Clang/LLVM: x86-64-only OpenDirectory, consistent objc4 debug macros, older Objective-C collection code generation and LLVM 22/23 branch opcode compatibility.

- **Video scaling:** generated ELF GL resolvers lost floating-point arguments on the first call. Chromium's first linear sampler request failed, leaving nearest sampling. Preserving registers fixes the native first-call sampler/color probe; the old wrapper fails both checks. An 800×450 checkerboard scaled to 640×360 changes to bilinear sampling.
- **Startup presentation:** drawing another differently sized surface in the same GL context stopped the active async presenter. Those surfaces now fall back to readback without stopping it. The old build fails after three visible frames; the corrected native probe retains the presenter through 357 fallback frames and presents all 360 frames at about 60 FPS. User confirmation of the splash remains pending.
- **UI clicks:** window dragging now examines the frontmost view by geometry, including Electron's control-region overlays that deliberately return nil from hit testing. Previously a click could reach the underlying draggable web view and start a drag instead of activating a control.
- **External-link crashes:** six LaunchServices paths dereferenced nullable error-output pointers. They now check the pointers and populate supplied outputs. AppKit also implements the missing URL-opening configuration/completion methods, and LaunchServices hands `http`, `https` and `mailto` URLs to the host desktop (portal, then a transient systemd unit running `xdg-open`). Native nullable-output checks and the host-side D-Bus hand-off regression passed. In a fresh guest prefix, `open https://…` and `NSWorkspace -openURL:configuration:completionHandler:` both reached the portal with the exact URL and opened the host browser; a link clicked in the Riot client is not yet confirmed. The paired installer covers Electron's absolute LaunchServices subframework path as well as the flat override.
- **Installation:** the default prefix gets a writable `/private/tmp`; the first-time launcher skips the inaccessible native intro and avoids an installed-client `--app-root`. The fetcher accepts missing informational `Info.plist` metadata. A separate HTTP 409 LaunchAgent handoff failure remains unresolved in the manual launch path.

The historical update hook handles HTTP 409, interrupted `pending_verify` and active downloads in 28 mocked scenarios. Historical external-link forwarding was exercised before the workflow changed. Both inspector-based helpers are now disabled in normal launches. Native URL methods no longer queue requests in a spool; LaunchServices hands them to the host desktop. The window-visibility correction now comes from Darling's AppKit overlay rather than a patch to Riot's binaries; automated practice/profiling tools remain excluded from recommended setup/testing.

## FPS observation and server optimization

An 18-minute host observation found substantial Darling request overhead and repeated main-thread socket waits consistent with its RPC path. The user reported 100–120 FPS in quieter moments and 70–80 during fights, versus 170–190 on Windows. GPU utilization averaged about 61% with an 85% maximum. CPU temperatures were 92–94°C, with reported clocks averaging 4.13 GHz. Background clients remained busy; the server still used about 1.5 logical CPUs after the observed game exited.

Server-only CPU samples identified worker context switching and receive/message allocation as substantial costs. These observations do not establish the entire FPS gap, distinguish every intentional game wait from added RPC latency, or prove a per-frame cause.

The receive queue now retains unused receive slots, transfers real messages into the existing queue and adopts every returned control header before potentially throwing. This preserves received descriptor ownership even if deque allocation fails. Disabled debug logging no longer prepares PID/TID strings. Independent host socket checks reduce warmed empty receive allocations from 32 to zero and single-packet allocations from 64 to two. Normal and address/undefined-behavior sanitizer checks passed, including exception cleanup and descriptor ownership.

The normal launcher no longer enables a client inspector, starts scheduling/update/link helpers or forces unlimited UI frames. The user installed the new server atomically while the old server remained active. Later matches measured the gains of this and the following changes; see [Game FPS, 2026-10-13/14](#game-fps-2026-10-1314).

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

Historical short Baron runs measured 99.6 FPS average / 16.11 ms p99 and 85.7 FPS average / 23.51 ms p99 in a spell-heavy attempt. Refresh/camera differences and old reset helpers make those comparisons unreliable. They do not demonstrate a 90 FPS p99 result or an ARAM guarantee. Laptop-specific CPU affinity is not installed automatically.
