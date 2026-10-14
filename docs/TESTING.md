# Testing on another PC

Run standalone probes and offline mocks; launch and play matches manually. Do not attach a debugger, query client APIs, inject diagnostic client/game hooks or automate input. Do not stop a running client/game to test a runtime change. Presentation probes should use their own windows with clients closed, since the native presentation fixtures use shared per-user target names.

## Patch and offline checks

After preparing the pinned source tree:

```sh
python3 scripts/prepare.py --check-only
python3 tools/riot-launcher-offline-check.py
python3 tools/riot-memory-installer-offline-check.py
python3 tools/riot-foundation-installer-offline-check.py
node tools/riot-league-update-check.js
node tools/riot-open-url-check.js
```

The patch check uses temporary base indexes and does not reset or modify the source checkout. The launcher check uses stub commands and never starts or contacts Riot or Darling. The memory-installer check imports the installer and exercises its policy (default source is the prepared artifact; the artifact is never a destination of a different library) in temporary directories. The Foundation-installer check does the same for the overlay installer: it seeds a temporary framework from a stub SDK, replaces `Versions/C/Foundation`, keeps the standard symlinks, stages a backup, treats a re-run as current and refuses a non-Mach-O source. The JavaScript checks use mock Electron/filesystem/HTTP objects; they do not enable the legacy hooks in a real client. The installer handoff mock covers 28 scenarios, including HTTP 409, interrupted verification and preserving active downloads.

## Standalone guest probes

Build and install the base/runtime first. Native probes are built by the incremental runtime scripts. With the default guest username, run a probe like this (replace `me` and the host checkout path):

```sh
darling shell env \
  DYLD_FRAMEWORK_PATH=/Users/me/RuntimeFrameworks \
  DYLD_LIBRARY_PATH=/Users/me/RuntimeLibraries \
  /Volumes/SystemRoot/path/to/darling-lol/build/riot-appkit-pump-check
```

Build additional probes when needed, for example:

```sh
python3 build/runtime/tools/build-riot-metal-concurrency-check.py
```

Useful probes (build tokenized Python helpers from `build/runtime/tools/`):

- `riot-launchservices-null-error-check`: nullable LaunchServices error outputs must not crash, and a supplied empty output must receive an error. Build with `build-riot-launchservices-check.py`; expected `nullable_outputs=1 error_populated=1 missing_handlers=1`.
- `riot-appkit-pump-check`: every queued callback must arrive in its own event poll; expected `events=200 callbacks=200 late_polls=0`.
- `riot-event-coalescing-check`: preserve key events and latest mouse motion.
- `m327-cgl-resolver-check`: first calls through dynamically looked-up GL functions retain floating-point arguments; expected `first_sampler_linear=1 first_color_arguments=1 errors=0,0`. Build with `build-m58f327-opengl.py` and run with `run-riot-native-check.py m327-cgl-resolver-check`.
- `m327-cgl-direct-check`: with `RCLOL_GPU_DIRECT_OTHER_WINDOW_TEST=1` and an owned 337x339 native target, interleave 338x339 fallback surfaces; expected `active_presenter_preserved=1 fallback_frames=357`. This needs the native presentation fixture; it is not a `run-riot-native-check.py` choice.
- `riot-metal-presentation-check`: Vulkan clear reaches the GL texture, including late window/context attachment.
- `riot-metal-concurrency-check`: submission ordering, resource lifetime and concurrent recording.

`RIOT_PRESENTATION_STRESS=1` makes the presentation probe run 120 frames. With the game and clients closed, force async encoding for this standalone probe using the guest flag `/tmp/metal-async-encode`, then remove it. The tested old path leaked 120 synchronization FDs over 120 frames; the fixed path leaked zero. Inspect the probe's host `/proc/PID/fd` for `anon_inode:syncobj_file` if reproducing that test.

## Host receive queue regression

The receive-buffer regression runs entirely on independent host sockets:

```sh
c++ -std=c++17 -O2 -pthread \
  -I build/darling-src/src/external/darlingserver/internal-include \
  tools/riot-host-receive-check.cpp \
  build/darling-src/src/external/darlingserver/src/message.cpp \
  -Wl,--wrap=malloc -o build/riot-host-receive-check
build/riot-host-receive-check
python3 tools/riot-launcher-offline-check.py
```

Expected: `warm_empty_allocations=0 single_packet_allocations=2`, correct credentials, descriptor ownership/CLOEXEC, sender addresses, multiple batches, truncation flags, callback reentrancy and exception cleanup. The old receive implementation makes 32 allocations for a warmed empty receive and 64 for a single-packet receive in the same fixture. These are allocation counts, not an FPS benchmark. The launcher check uses stub commands and never starts or contacts Riot or Darling.

The same source passed AddressSanitizer/UndefinedBehaviorSanitizer and leak detection. Allocation counts depend on this fixture/compiler/library; preserve the no-wasted-slot property rather than interpreting them as game performance. See [setup](SETUP.md#updating-an-existing-runtime) for server build/install/activation.

## Host reply path regression

The direct-reply checks run entirely on independent host sockets and exercise the real `src/message.cpp` (`MessageQueue::pushAndSend`, `sendMany`, `empty`):

```sh
c++ -std=c++17 -O2 -pthread \
  -I build/darling-src/src/external/darlingserver/internal-include \
  tools/riot-host-reply-check.cpp \
  build/darling-src/src/external/darlingserver/src/message.cpp \
  -o build/riot-host-reply-check
build/riot-host-reply-check
```

Expected: `HOST_REPLY pass=1 direct_send=ok eagain_retry=ok fifo=ok concurrency=ok notify=ok descriptors=ok dead_peer=ok`. It covers: a direct send that drains the queue does not invoke the wakeup callback while a queued message does; EAGAIN leaves the queue intact and the retry path delivers every message in order; four concurrent pushers plus a draining "main loop" preserve per-pusher order without loss or duplication; descriptors are delivered exactly once, closed on the sender side right after the send and released on the receiver side; and a dead peer (ENOENT after unlink, ECONNREFUSED after close) drops only its own message without wedging the queue or leaking descriptors. Compile the same way with `-fsanitize=address,undefined` and `-fsanitize=thread`; the ThreadSanitizer run is the important one.

A same-process microbenchmark compares the direct path with the old `push` + eventfd + main-loop `sendMany` hand-off:

```sh
c++ -std=c++17 -O2 -pthread \
  -I build/darling-src/src/external/darlingserver/internal-include \
  tools/riot-host-reply-bench.cpp \
  build/darling-src/src/external/darlingserver/src/message.cpp \
  -o build/riot-host-reply-bench
build/riot-host-reply-bench 5000
```

Expected: a `HOST_REPLY_BENCH same_process=1 ...` line; the direct path is a few microseconds faster on average. Both endpoints are threads of one process, so this isolates the hand-off, not real guest scheduling.

## Memory reclamation regression

The memory-reclamation check links the actual Darling `sys_madvise` source to host syscall hooks and a `read_string` stub that selects the host's transparent-huge-page policy, and uses only its own 32 MiB anonymous allocation:

```sh
python3 tools/build-riot-host-madvise-check.py
```

The build script runs the check once per policy. Expected:

```
HOST_MADVISE pass=1 mode=always madvise_calls=6 sub_page_syscalls=0 reclaimable_KiB=32768 reuse_reclaimable_KiB=0 anon_huge_KiB=32768/32768 contents=preserved
HOST_MADVISE pass=1 mode=never ... reclaimable_KiB=32768 reuse_reclaimable_KiB=0 ...
HOST_MADVISE pass=1 mode=madvise ... reclaimable_KiB=32768 reuse_reclaimable_KiB=0 ...
```

`mode=always` covers the deployment host: sub-huge-page hints reach no `madvise` syscall and do not reduce the mapping's `AnonHugePages` (read from `/proc/self/smaps`), the 2 MiB-aligned region is marked reclaimable, an unaligned large range is rounded inward to the contained whole huge pages, and reuse cancels reclamation with contents preserved. `mode=never`/`mode=madvise` prove the full range is still forwarded when the host does not give this layer's mappings huge pages. The old implementation returned success without marking any pages.

Darwin `MADV_FREE` and `MADV_FREE_REUSABLE` map to Linux `MADV_FREE` for whole 2 MiB huge pages only; Darwin `MADV_FREE_REUSE` uses `MADV_POPULATE_WRITE` over the full requested range to cancel lazy reclamation while preserving surviving contents. The reuse operation requires Linux 5.14 or newer; an unsupported or invalid operation returns the translated kernel error rather than reporting false success. Reclamation is lazy and does not promise an immediate RSS decrease when RAM is available.

For the actual guest ABI, `tools/build-riot-madvise-check.py` builds an independent executable. After a full restart has activated the installed library, run it as follows; replace `me` and the checkout path:

```sh
python3 tools/build-riot-madvise-check.py
darling shell env \
  DYLD_LIBRARY_PATH=/Users/me/RuntimeLibraries \
  /Volumes/SystemRoot/path/to/darling-lol/build/logs/madvise-fix/riot-madvise-check
```

Expected: `DARLING_MADVISE pass=1 reclaimable_KiB=4096 reuse_reclaimable_KiB=0 contents=preserved`. The probe marks a 2 MiB-aligned 4 MiB region inside a larger mapping, because only whole huge pages are forwarded; it uses only its own allocation and Linux memory counters, and never contacts a client/game.

## Host vDSO clock regression

The clock check compiles the actual `emulation/src/linux_premigration/ext/clock_gettime.c` against a stub ELF bridge backed by the host's `dlopen`/`dlsym`:

```sh
python3 tools/build-riot-host-vdso-clock-check.py
build/logs/vdso-clock/host-vdso-clock-check default
build/logs/vdso-clock/host-vdso-clock-check no-symbol
build/logs/vdso-clock/host-vdso-clock-check race
```

Expected (costs vary with machine load):

```
HOST_VDSO_CLOCK pass=1 mode=default syscall_ns=528 vdso_ns=24 dlopen_calls=1 dlsym_calls=1 million_reads=1 threads=8 fork=preserved
HOST_VDSO_CLOCK pass=1 mode=no-symbol ... (syscall fallback, correct values)
HOST_VDSO_CLOCK pass=1 mode=race ... dlopen_calls=1 ...
```

`default` reads the clock before the bridge exists (syscall fallback, not cached), resolves the vDSO entry once the bridge appears, checks a million reads per clock id against the raw syscall and across eight threads, and checks that a forked child inherits the resolved pointer. `no-symbol` proves the fallback when the bridge has no entry. `race` starts eight threads before resolution and requires that only one resolution ran.

Install the tested library with `python3 tools/install-riot-memory-runtime.py`. Its default source is the optimized relink `build/riot-libsystem_kernel.dylib`; if it is missing, build it first with `python3 build/runtime/tools/build-riot-kqueue-runtime.py`. The installer uses user-owned `build/memory-runtime-backups` instead of the shared `build/backups` directory, which an earlier sudo server install may have made root-owned. It atomically replaces the two per-user kernel-library copies, without stopping any process or changing the system SDK. The prepared artifact is never a destination of a different library: a run with an explicit `--binary` replaces the two prefix copies and leaves `build/riot-libsystem_kernel.dylib` untouched (the check `tools/riot-memory-installer-offline-check.py` covers this). The active Darling overlay can retain the old file even for new guest processes after an update to its backing directory; this was verified by differing host versus namespace checksums. After exiting the game and both clients manually, run `darling shutdown`, then `sh scripts/run.sh` to start a new namespace. Do not restart Darling during a match. Verify reclamation and client memory use after that restart.

## CPU topology regression

The hw.* / machdep.cpu.* handlers are checked on the host against the host's real topology. The check links the actual `sysctl_hw.c` and `sysctl_unspec.c` to host shims (real `/sys` reads, forwarded syscalls, sentinel Mach replies) and drives each handler directly plus `sysctlbyname("hw.*")` through the real name-to-oid lookup:

```sh
python3 tools/build-riot-host-sysctl-cpu-check.py
```

Expected on this 6-core/12-thread host:

```
HOST_SYSCTL_CPU pass=1 mode=normal ncpu=12 active=12 logicalcpu=12 logicalcpu_max=12 physicalcpu=6 physicalcpu_max=6 model=MacBookPro16,1
HOST_SYSCTL_CPU pass=1 mode=affinity ncpu=12 active=4 logicalcpu=4 logicalcpu_max=12 physicalcpu=4 physicalcpu_max=6 model=MacBookPro16,1
HOST_SYSCTL_CPU pass=1 mode=no-detect ncpu=8 active=8 logicalcpu=8 logicalcpu_max=8 physicalcpu=8 physicalcpu_max=8 model=MacBookPro16,1
HOST_SYSCTL_CPU pass=1 mode=optout ncpu=8 active=8 logicalcpu=8 logicalcpu_max=8 physicalcpu=8 physicalcpu_max=8 model=MacBookPro16,1
```

`normal` is the deployment case: hw.ncpu/hw.logicalcpu_max/hw.physicalcpu_max are the host's online counts, hw.activecpu/hw.availcpu/hw.logicalcpu/hw.physicalcpu follow this process's affinity mask. `affinity` restricts the check to four CPUs first, so avail/active fall to 4 while the max values stay at 12 (and 6). `no-detect` fails every `/sys` CPU read and `sched_getaffinity`, `optout` sets the flag file (a check-local path compiled into that binary); both must fall back to the old fixed 8s. The emulated `hw.model` must stay `MacBookPro16,1` in every mode. The production opt-out flag is `/tmp/rclol-fixed-cpu-count`; the host check overrides only the path, never touches the real file.

The commpage fields the same change publishes (`_COMM_PAGE_NCPUS`, `_COMM_PAGE_ACTIVE_CPUS`, `_COMM_PAGE_PHYSICAL_CPUS`, `_COMM_PAGE_LOGICAL_CPUS` and the `kNumCPUs` capability bits) come from mldr. `build/riot-mldr` is the rebuilt loader; activating it means replacing the root-owned SDK copy (`/usr/local/libexec/darling/usr/libexec/darling/mldr`) after exiting clients and shutting down Darling, as in the setup notes.

## Optimized library overlays and string primitives

The string primitives the guest resolves to are checked on the host against two copies of the same libplatform sources, a byte-loop reference, and an unmapped page on each side of every buffer:

```sh
python3 tools/build-riot-platform-memory-check.py
```

Expected (five runs: the generic path, the vector path, the vector plus REP path, then the same check built with AddressSanitizer and UndefinedBehaviorSanitizer):

```
PLATFORM_MEMORY_CHECK pass=1 capabilities=0 cases=194680 mismatches=0 page_faults=0 variants=2
PLATFORM_MEMORY_CHECK pass=1 capabilities=0x20000000 cases=194680 mismatches=0 page_faults=0 variants=2
PLATFORM_MEMORY_CHECK pass=1 capabilities=0x28000000 cases=194680 mismatches=0 page_faults=0 variants=2
PLATFORM_MEMORY_CHECK pass=1 capabilities=0 cases=194680 mismatches=0 page_faults=0 variants=2
PLATFORM_MEMORY_CHECK pass=1 capabilities=0x28000000 cases=194680 mismatches=0 page_faults=0 variants=2
```

`current` is the code and flags the installed `libsystem_platform.dylib` was built from and `new` is what `tools/build-riot-platform-strings.py` produces; every case runs against both and against the reference, for every size up to 512 with a subset of the 64x64 alignments, for random sizes and alignments, for both overlap directions, and for buffers that only just fit the guarded region. The same script builds `build/riot-platform-memory-bench`, which measures the same functions against the host's glibc for every size from 1 to 256 and at 512 B to 1 MiB (`BENCH size=... op=... current_ns=... new_ns=... glibc_ns=...`). That is a throughput measurement of the primitives, not an FPS measurement.

Which file the guest actually loads for a library, and how that file was compiled, is listed by:

```sh
python3 tools/riot_overlay_audit.py
```

An overlay is compared with the library it would replace by:

```sh
python3 tools/riot_overlay_verify.py build/riot-libsystem_c.dylib /usr/local/libexec/darling/usr/lib/system/libsystem_c.dylib
```

Expected `OVERLAY_VERIFY pass=1` plus the export counts and the stack-reference share before and after. The check requires the same install name and versions, the same dependent libraries, re-exports and segment protections, the same exported symbols (a weak definition the optimized build drops is reported as a note, because the ABI makes a weak definition optional), no new undefined symbol, and an improved optimization estimate. It never starts a guest process.

The batch installer's policy and rollback are exercised in temporary directories:

```sh
python3 tools/riot-libs-installer-offline-check.py
```

Expected: `LIBS_INSTALLER_OFFLINE pass=1 overlays=5 backup_before_replace=1 rollback_restores=1 rollback_removes=1 idempotent=1 rejects_non_macho=1`. It imports the installer, never executes it, and never touches the real prefix.

## Local semaphore model

```sh
sh tools/riot-lsem-model/run.sh
sh tools/riot-lsem-model/sensitivity-check.sh
```

`extract-lsem.py` copies the shipped local-semaphore table and trap functions verbatim out of the patched `mach_traps.c`; it refuses to run if the generation-checked token compare-exchange or the negative destroy marker is missing. The model compiles that code with shims for the raw syscalls and server RPCs. It runs create/destroy churn with name reuse on 8 threads, waiter/signaler pairs, timed waits, `signal_all`, destroy while waiting, and the XNU ordering of `wait_signal`. Expected: `LSEM_MODEL checks=686 failed=0 spurious_success=0 lost_wakeup=0 steal=0 hang=0`. The sensitivity check re-breaks the protocol in a copy and must report failures: positive tombstone (`failed=5 spurious_success=1`), missing generation bump (`failed=2 spurious_success=400`). Outputs go to `build/riot-lsem-model`.

## Metal bind model

```sh
sh tools/riot-metal-bind-model/run.sh
```

The bind-tracking helpers are extracted verbatim from `MTLRenderCommandEncoder.mm`. The model drives two encoder states, one receiving every call and one only the forwarded calls, and requires them to be equal after every call. That covers directed scenarios, among them a freed wrapper whose address is reused by a different resource and depth-stencil retention, plus 119,401 random calls. Expected: `randomized calls=119401 forwarded=110294 dropped=9107 (7.6%)`, `BIND_DEDUP_MODEL failures=0` and `BLIT_CAST_MODEL failures=0`.

## Passive match capture

These tools observe a manually played match from the host. They wait for the game process, stop when it exits, and never attach to it, write to it, or read its memory.

```sh
python3 tools/riot-match-capture/sample-match.py           # start before the match; writes build/logs/host-sample-<stamp>.jsonl
python3 tools/riot-match-capture/sample-report.py build/logs/host-sample-<stamp>.jsonl
python3 tools/riot-match-capture/compare-matches.py \
  "before=build/logs/host-sample-A.jsonl:$HOME/.darling/Applications/League of Legends.app/Contents/LoL/Logs/GameLogs/<A>" \
  "after=build/logs/host-sample-B.jsonl:$HOME/.darling/Applications/League of Legends.app/Contents/LoL/Logs/GameLogs/<B>"
```

`sample-match.py` reads only `/proc` and `/sys`. Per thread of the game and of darlingserver it records scheduler run, runqueue and wakeup counts every second, and the system call each thread is blocked in (50 Hz for the main thread, 10 Hz for the others). It also records GPU busy percentage, the game's huge-page share every 30 seconds, and whether another of our threads runs on the main thread's SMT sibling. `compare-matches.py` aligns the capture with the game's 10-second `netstats.csv` rows. It prints average FPS, the worst row, the slowest quarter and, per frame, the main thread's CPU time, server waits, futex waits and blocks.

For an instruction-address profile, which needs `perf_event_paranoid` ≤ 2 (user-space samples only), build the sampler once and run the wrapper instead:

```sh
c++ -O2 -std=c++17 -o build/riot-ip-profiler tools/riot-match-capture/ip-profiler.cpp
python3 tools/riot-match-capture/profile-match.py
python3 tools/riot-match-capture/profile-report.py build/logs/host-profile-<stamp> 120 4
python3 tools/riot-match-capture/profile-symbols.py build/logs/host-profile-<stamp> <main tid> Metal libobjc.A.dylib libindium.dylib
```

The profiler samples each thread's user-space instruction pointer every 1 ms on the main thread and every 4 ms on the others, with no call stacks and no registers. The wrapper records the memory map every 15 seconds so the report can attribute samples to modules. Ask the person playing before profiling their match.

## Manual game observation

Play matches manually through Riot. Check sound, keyboard/mouse buttons, fullscreen confinement, camera motion and returning to the client. The legacy `baron-setup.py`, `baron-reset.py`, `profile-cycle.sh`, inspector scheduling/update/URL helpers and Electron binary patching tools are excluded from this workflow. Normal launches enable none of these helpers and do not force unlimited UI frames. Automatic installer handoff and host external-link forwarding are consequently unavailable.

Observe host CPU/thread scheduling, GPU counters, temperatures and memory pressure without controlling the client/game. Record manual FPS readings with timestamps and comparable graphics settings. Do not interpret a socket wait as avoidable RPC latency without distinguishing intentional synchronization. Server-only CPU sampling identifies costs but does not measure per-frame causality.

Compare similar scene, ability and camera activity with the same power settings (and correct charger on a laptop). Do not report a paused, dead, unfocused or empty scene as a fight benchmark. If an allowed host measurement supplies frame times, report p99 in milliseconds; its reciprocal is percentile-equivalent FPS, not the mean of the slowest 1% of frames. Manual FPS readings alone do not establish p99 frame times.

The CPU symbolizer offers `--leaf-only` because the host DWARF unwinder can drop guest Mach-O stacks. Successfully unwound samples alone are not a representative profile. Local API lockfiles, account data and raw logs must stay outside version control.
