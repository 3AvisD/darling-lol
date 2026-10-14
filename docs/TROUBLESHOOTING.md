# Troubleshooting

Use the [setup instructions](SETUP.md) from the repository root as your ordinary user. Exit the game and both clients manually before a full installation or Darling restart. Incremental server/memory installers can replace files without interrupting processes, but their changes need a later full restart to activate.

## Memory installer: permission denied

An earlier sudo server installation can create root-owned `build/backups`. The old memory installer tried to create `build/backups/memory-runtime` there and failed at `backup_folder.mkdir(...)`. The corrected default is a separate user-owned `build/memory-runtime-backups` directory:

```sh
python3 tools/install-riot-memory-runtime.py
```

For an explicitly chosen backup location:

```sh
python3 tools/install-riot-memory-runtime.py --backup-dir "$HOME/darling-memory-backups"
```

If the destination library itself is root-owned, inspect the named path and its ownership before retrying with sudo. Do not run all setup/build steps as root or recursively change ownership of the system SDK. Backups and binaries are ignored by Git and must stay local.

## Updated library or server still behaves like the old version

Atomic replacement preserves active process mappings. Darling also uses an overlay filesystem; updating its backing prefix from the host can leave an old file visible inside the active namespace. A fresh standalone guest check continued to report the old memory behavior despite the host path having the new checksum. Reopening only Riot does not replace this namespace.

After exiting the game and both clients manually:

```sh
darling shutdown
sh scripts/run.sh
```

Do not run these commands during a match. Use the independent reclamation probe in [testing](TESTING.md) to verify the new guest behavior after activation. An installer result with `installed: true` and `restart_performed: false` confirms the disk update, not activation.

## Around 5 GB of RAM with just the clients open

The observed proportional resident footprint was roughly 4.3 GiB across Riot, League and Darling, including 50 crash-handler processes. Roughly 3 GiB came from private anonymous mappings/native heaps; graphics mappings accounted for about 238 MiB. Large virtual address reservations are not equivalent to resident RAM, and summed RSS double-counts shared pages. Use proportional set size (PSS), swap and host memory pressure to assess the actual footprint.

The old kernel library returned success for Darwin allocator free/reuse hints without making any pages reclaimable. The replacement maps free/reusable hints to Linux `MADV_FREE`, and reuse to `MADV_POPULATE_WRITE`. The standalone 4 MiB buffer becomes reclaimable and returns to normal on reuse while preserving surviving data. This fixes a demonstrated runtime behavior, but does not explain all client allocations or prove a specific RAM reduction. Linux may retain lazily reclaimable pages in RSS while RAM is available. A five-minute idle observation did not show rapid client memory growth.

Build/install the kernel library using [the incremental update steps](SETUP.md#updating-an-existing-runtime), then perform the manual restart when finished playing. Linux 5.14+ is required. Do not kill client helpers as an attempted memory optimization during play.

## Installer exits, cannot write temporary files, or Install appears idle

Use `sh scripts/installer.sh euw` (or your supported region). The first-time launcher must omit the installed client's `--app-root` and pass `--skip-to-install`; the normal wrapper now does this. It also selects `league_of_legends` so the product installation UI appears after login.

If logs report guest `/private/tmp` permission errors, the default prefix needs its own temporary directory. The corrected full runtime installer creates it. To repair just this default-prefix directory as your ordinary user:

```sh
mkdir -p "$HOME/.darling/private/tmp"
chmod 1777 "$HOME/.darling/private/tmp"
```

The installer fetcher no longer treats a missing archive `Info.plist` or the informational `unzip | head` listing as a fatal download failure. Download and extraction errors still fail. The wrapper invokes this fetcher with Bash, since its `pipefail` setting is unsupported by some `/bin/sh` implementations.

There is also a separate known compatibility gap: native LaunchAgent watch registration can return HTTP 409 after the League install path is accepted, leaving the download idle. A historical inspector hook handles this and interrupted `pending_verify` states in 28 mock cases, but that hook is disabled in normal launches and excluded from the manual workflow. There is no verified runtime-only replacement for that handoff in this snapshot. Do not assume the native permission and launcher fixes resolve every idle Install case, or enable debugger/API automation to work around it.

## UI crashes after a button opens an external link

LaunchServices dereferenced a nullable error-output pointer in six paths. The patch checks the pointer itself and populates supplied outputs; the standalone regression covers missing handlers and null outputs. Electron opens LaunchServices through an absolute CoreServices subframework path, so the paired installer also replaces that binary in the stopped guest prefix. Updating only `RuntimeFrameworks/LaunchServices.framework` can leave the crash unfixed.

Historical host-browser forwarding depended on an inspector hook and is disabled in normal launches. The native null-pointer fix prevents this demonstrated crash; it does not guarantee that every external link opens on the host or explain every unrelated button crash.

## Blocky video or flickering startup logo

The first dynamically resolved GL call could lose floating-point arguments during ELF symbol lookup. Chromium's linear sampler request then failed and left nearest sampling. The generated resolver now preserves those registers. Install both the rebuilt native `libGL.dylib` in `RuntimeLibraries` and the OpenGL framework's library copy using the paired runtime installer; the framework alone is insufficient.

A separate multi-window path stopped the active async presenter when another differently sized surface was drawn in the same context. Such surfaces now use readback without stopping it. Native regressions cover both fixes; user confirmation of the startup splash remains pending. See [testing](TESTING.md) for the first-call sampler and interleaved-window probes.

## Build fails on a new compiler or runs out of memory

- **objc4 debug macro mismatch:** use the empty build type from `scripts/build.sh`, rather than `Release`/`NDEBUG`.
- **OpenDirectory 32-bit ivar errors:** keep `TARGET_i386=OFF`; the supported client is x86-64.
- **Undefined empty Objective-C collection symbols:** retain the patched macOS 10.15 Objective-C runtime code generation.
- **LLVM branch opcode errors:** retain the Indium LLVM 22/23 compatibility patch; do not replace its branch handling with an unconditional version-specific opcode.
- **Build process killed:** lower parallelism, for example `JOBS=2 ./scripts/build.sh` or `--parallel 1` for an incremental component build.
- **Incremental helper cannot find `flags.make` or `link.txt`:** use Unix Makefiles, not Ninja.
- **Prebuilt Swift file contains Git LFS pointer text:** install Git LFS and fetch the original upstream assets; pointer text is not a usable library.

## Lower fight FPS than Windows

The game code itself runs natively under Darling: the x86-64 slice of the universal binary executes unchanged, picks its instruction-set paths with `cpuid` on the real CPU, and sizes its worker pool from the real logical CPU count. What differs from Windows is the path around it. Every Metal call goes through Objective-C, the Metal wrapper, Indium and Vulkan instead of a native Direct3D driver, and kernel services go through the emulation and darlingserver. In an ARAM fight the game's own code now takes most of a frame; see [status](STATUS.md#game-fps-2026-10-1314) for measurements.

If FPS drops back towards the 10-13 morning numbers (75–85 on Hexgate):

1. Make sure the current kernel library, Metal and Indium overlays are installed and active: exit the game and both clients, then `darling shutdown` and `sh scripts/run.sh` ([setup](SETUP.md#game-process-switches)).
2. Check the game's line in `/tmp/rclol-game-decisions.log`: it should read `local_semaphores=1` and `core_isolation=0`. No `/tmp/rclol-no-local-*` file should exist unless you created it on purpose.
3. Capture a match with `tools/riot-match-capture/sample-match.py` and compare it with an earlier one using `compare-matches.py` ([testing](TESTING.md#passive-match-capture)). A server wait above about 0.5 ms per frame means one of the local paths is off. A runqueue wait above a few percent on the main thread means the CPU is oversubscribed, for example by `/tmp/rclol-core-isolation` or background load.

Compare matches on the same map: Bilgewater runs a few percent below Hexgate.

On Hyprland, also check that direct scanout is off (`hyprctl getoption render:direct_scanout` prints `int: 0`). The first match with it off ran about 16 FPS faster than the previous one on the same build; see the next section.

## Cursor stops changing shape in fullscreen

On Hyprland the game's cursor can keep its first shape (for example, it never switches to the attack cursor) until you leave and re-enter fullscreen. Darling sends every change: `tools/riot-cursor-watch/cursor-watch.c` logs the X server's displayed cursor and saw about 1,500 changes in a 12-minute match. What freezes the shape on screen is Hyprland's direct scanout of the fullscreen Xwayland window. Turn it off in `hyprland.conf`:

```
render {
    direct_scanout = 0
}
```

`hyprctl keyword render:direct_scanout 0` applies it until Hyprland restarts. To check the X side yourself, build and run the watcher while playing:

```sh
cc -O2 -o build/riot-cursor-watch tools/riot-cursor-watch/cursor-watch.c -lX11 -lXfixes
build/riot-cursor-watch
```

It prints one line per cursor change. It only listens for X server notifications and does not touch the game.
