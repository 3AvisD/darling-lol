# Testing on another PC

Run standalone probes and offline mocks; launch and play matches manually. Do not attach a debugger, query client APIs, inject diagnostic client/game hooks or automate input. Do not stop a running client/game to test a runtime change. Presentation probes should use their own windows with clients closed, since the native presentation fixtures use shared per-user target names.

## Patch and offline checks

After preparing the pinned source tree:

```sh
python3 scripts/prepare.py --check-only
python3 tools/riot-launcher-offline-check.py
node tools/riot-league-update-check.js
node tools/riot-open-url-check.js
```

The patch check uses temporary base indexes and does not reset or modify the source checkout. The launcher check uses stub commands and never starts or contacts Riot or Darling. The JavaScript checks use mock Electron/filesystem/HTTP objects; they do not enable the legacy hooks in a real client. The installer handoff mock covers 28 scenarios, including HTTP 409, interrupted verification and preserving active downloads.

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

## Memory reclamation regression

The memory-reclamation check links the actual Darling `sys_madvise` implementation to host syscall hooks and uses only its own 4 MiB anonymous allocation:

```sh
python3 tools/build-riot-host-madvise-check.py
build/logs/madvise-fix/host-madvise-check
```

Expected: `reclaimable_KiB=4096 reuse_reclaimable_KiB=0 contents=preserved`. The old implementation returns success without marking any pages reclaimable. Darwin `MADV_FREE` and `MADV_FREE_REUSABLE` now map to Linux `MADV_FREE`; Darwin `MADV_FREE_REUSE` uses `MADV_POPULATE_WRITE` to cancel lazy reclamation while preserving surviving contents. The reuse operation requires Linux 5.14 or newer; an unsupported or invalid operation returns the translated kernel error rather than reporting false success. Reclamation is lazy and does not promise an immediate RSS decrease when RAM is available.

For the actual guest ABI, `tools/build-riot-madvise-check.py` builds an independent executable. After a full restart has activated the installed library, run it as follows; replace `me` and the checkout path:

```sh
python3 tools/build-riot-madvise-check.py
darling shell env \
  DYLD_LIBRARY_PATH=/Users/me/RuntimeLibraries \
  /Volumes/SystemRoot/path/to/darling-lol/build/logs/madvise-fix/riot-madvise-check
```

Expected: `DARLING_MADVISE pass=1 reclaimable_KiB=4096 reuse_reclaimable_KiB=0 contents=preserved`. To test a candidate before installation, place only its `libsystem_kernel.dylib` in a private directory and prepend its guest-visible path to `DYLD_LIBRARY_PATH`. The candidate passed; the previous installed implementation failed with zero reclaimable pages. The probe uses only its own allocation and Linux memory counters, and never contacts a client/game. It tests free/reuse hints directly, not complete Apple malloc or client behavior under memory pressure.

Install the tested library with `python3 tools/install-riot-memory-runtime.py`. It uses user-owned `build/memory-runtime-backups` instead of the shared `build/backups` directory, which an earlier sudo server install may have made root-owned. It atomically replaces the two per-user kernel-library copies and the prepared runtime artifact, without stopping any process or changing the system SDK. The active Darling overlay can retain the old file even for new guest processes after an update to its backing directory; this was verified by differing host versus namespace checksums. After exiting the game and both clients manually, run `darling shutdown`, then `sh scripts/run.sh` to start a new namespace. Do not restart Darling during a match. Verify reclamation and client memory use after that restart.

## Manual game observation

Play matches manually through Riot. Check sound, keyboard/mouse buttons, fullscreen confinement, camera motion and returning to the client. The legacy `baron-setup.py`, `baron-reset.py`, `profile-cycle.sh`, inspector scheduling/update/URL helpers and Electron binary patching tools are excluded from this workflow. Normal launches enable none of these helpers and do not force unlimited UI frames. Automatic installer handoff and host external-link forwarding are consequently unavailable.

Observe host CPU/thread scheduling, GPU counters, temperatures and memory pressure without controlling the client/game. Record manual FPS readings with timestamps and comparable graphics settings. Do not interpret a socket wait as avoidable RPC latency without distinguishing intentional synchronization. Server-only CPU sampling identifies costs but does not measure per-frame causality.

Compare similar scene, ability and camera activity with the same power settings (and correct charger on a laptop). Do not report a paused, dead, unfocused or empty scene as a fight benchmark. If an allowed host measurement supplies frame times, report p99 in milliseconds; its reciprocal is percentile-equivalent FPS, not the mean of the slowest 1% of frames. Manual FPS readings alone do not establish p99 frame times.

The CPU symbolizer offers `--leaf-only` because the host DWARF unwinder can drop guest Mach-O stacks. Successfully unwound samples alone are not a representative profile. Local API lockfiles, account data and raw logs must stay outside version control.
