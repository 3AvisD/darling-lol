# darling-lol

Experimental source patches and tools for running the **macOS League of Legends client on x86-64 Linux through Darling**.

This is a cleaned snapshot of a working development setup, not a finished launcher. Riot and League clients can launch, practice/ARAM games have run, and graphics, audio, input and post-game freezes have received compatibility fixes. Fight FPS still varies: **90 FPS at the 99th percentile has not been achieved**. A clean build on another PC has not yet been validated.

## Included

- Pinned upstream revisions and patches for 29 Darling components, including Metal/Indium, AppKit, QuartzCore, CoreAudio, the kernel shim and darlingserver.
- The latest event-poll presentation fix and compositor-thread semaphore import fix. The latter stopped one leaked GPU synchronization handle per frame in native tests.
- Build/install/launch wrappers with configurable machine paths, native regression probes and performance tools.
- Older experiments under `research/tools/`; these are retained for reference and are outside the normal launch path.

Game files, Riot installers, account databases, tokens, local prefixes, captures and compiled binaries are not included. Install the game through Riot on the second PC.

## Requirements

- x86-64 Linux, an X11 session or Xwayland, and a working hardware Vulkan/OpenGL driver. The development PC used AMD RADV; other drivers are not validated.
- Git + Git LFS, CMake, Make, Clang/LLD, LLVM development files, a C/C++ compiler, Python 3.12+ and the Darling build dependencies. LLVM 22 was used for the tested shader translator.
- libvpx development files and X11/XCB/SHM development files for the current runtime helpers.
- Enough free disk for Darling sources/build, Git LFS assets and the game: budget at least 50 GB. Building is substantially larger than this source-only repository.

Use the dependency list in the [pinned Darling source](https://github.com/darlinghq/darling/tree/60ba801decee7a00782f74f6be4c8ffb013f79ff) for your distribution. `tools/env-check.sh` provides a starting check; its legacy package names may need adapting to your distro.

## Build on the other PC

```sh
git clone https://github.com/lucannez64/darling-lol.git
cd darling-lol
python3 -m venv build/venv
build/venv/bin/pip install -r requirements.txt
python3 scripts/configure.py --guest-user "$USER"
./scripts/build.sh
sudo cmake --install build/darling-src/build
./scripts/install-runtime.sh
```

The default layout is `/usr/local/libexec/darling`, `~/.darling` and `/Users/$USER` inside the guest. Use the actual guest account name if it differs. Paths are rendered into ignored files under `build/runtime/tools`; do not run the tokenized `tools/` templates directly. See [setup details](docs/SETUP.md).

The install wrapper compiles the paired runtime, shuts down Darling before replacing per-user overrides, and backs up relevant files. Do this with no Riot/League session running.

## Install and launch Riot

After the runtime is installed:

```sh
./scripts/installer.sh euw
```

Sign in through Riot's own UI and install League. Then stop the client, apply the guarded visibility correction for the installed Electron build, and launch normally:

```sh
python3 build/runtime/tools/stop-darling-runtime.py
python3 build/runtime/tools/patch-riot-electron-visibility.py
./scripts/run.sh
```

The visibility correction verifies the exact original instruction bytes and refuses an unknown version. A future Riot update can require adapting this and other compatibility hooks.

For another region, set `RCLOL_REGION` and `RCLOL_LOCALE` before launch. For example `RCLOL_REGION=NA RCLOL_LOCALE=en_US ./scripts/run.sh`. Linux display selection follows `$DISPLAY`; the legacy launcher fallback is `:1`.

## Testing

Start with native regression checks, then a practice game. See [testing](docs/TESTING.md). The practice helpers recognize two verified UI panel sizes, preserve dynamic camera settings and stop on lost focus/unknown geometry. They still drive real input: avoid controlling the game at the same time.

FPS counters, game presentation requests and physical display refresh are different measurements. Record average and p99 frame times, the active scene, graphics settings, charger/power state and whether the game stayed focused. See [current status](docs/STATUS.md).

## Layout

```
scripts/             portable entry points
upstream/manifest.json  exact base revisions and patch ownership
patches/             current source changes, one patch per repository
tools/               active source templates and regression probes
research/tools/      historical experiments, not a supported workflow
docs/                setup, testing, status and licensing notes
build/               local sources, outputs and rendered tools (ignored)
```

Darling/upstream components retain their existing licenses; see [third-party notes](NOTICE.md). Original integration tooling is distributed under GPL-3.0; patches retain the licensing of their target files. This project is not affiliated with Riot Games.
