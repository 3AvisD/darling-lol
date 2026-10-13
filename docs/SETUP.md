# Setup and portability

The patch manifest pins Darling to `60ba801decee7a00782f74f6be4c8ffb013f79ff` and records every changed submodule's revision. `scripts/prepare.py` checks each revision before applying patches and never resets an existing checkout to make a patch apply. Re-running it accepts an already-applied patch.

Git LFS must be available before fetching the upstream submodules. The local development checkout had removed prebuilt Swift files from its working directory; that deletion is deliberately not exported. A fresh upstream checkout keeps its original Swift assets and licensing. Do not install Git LFS pointer text as a library.

`configure.py` renders `@PROJECT_ROOT@`, `@HOST_HOME@`, `@GUEST_HOME@`, `@GUEST_USER@`, `@DPREFIX@` and `@DARLING_SDK@` into ignored local copies. A build-directory symlink preserves the original scripts' relative build layout. The public source files remain machine-independent.

```sh
python3 scripts/configure.py --guest-user my-linux-user
# A custom SDK can be rendered for advanced setups:
python3 scripts/configure.py --sdk /usr/local/libexec/darling
```

The normal wrappers currently support the default `~/.darling` prefix and `/usr/local` base installation. The configurator exposes a custom prefix for developers, but several archived tools assume the default layout. Custom prefixes/install locations have not been tested end to end.

The build uses Unix Makefiles because the incremental runtime helpers read CMake `flags.make` and `link.txt`. Ninja is not a drop-in replacement for those helpers.

The supported build is x86-64 only (`TARGET_i386=OFF`). The patched OpenDirectory uses Objective-C ivars that the 32-bit fragile ABI cannot compile. Keep the empty `CMAKE_BUILD_TYPE` used by `scripts/build.sh`: selecting `Release` introduces `NDEBUG` while objc4 defines `OBJC_IS_DEBUG_BUILD=1`, causing a debug-macro mismatch. The base patch requests the macOS 10.15 Objective-C runtime code generation to avoid undefined `__NSArray0__struct` / `__NSDictionary0__struct` symbols with newer Clang. Indium handles both the LLVM 22 and LLVM 23 branch opcode names.

Linux 5.14 or newer is required for `MADV_POPULATE_WRITE`, used when Darwin's allocator reuses lazily reclaimable pages. The memory installer checks this requirement; do not bypass it.

The runtime installer requires native `pkg-config` entries for `vpx`, `x11`, `x11-xcb`, `xcb-shm` and `xext`, plus the base Darling SDK. Some bridge guards intentionally fall back to Chromium's own implementation when the installed client changes.

Run configuration, builds and the per-user runtime installation as your ordinary account. Use sudo only for the base CMake installation and the incremental server installer. Running the whole build or per-user install under sudo can create root-owned files and configure the wrong guest home. `scripts/install-runtime.sh` initializes the default user prefix and creates `~/.darling/private/tmp` with mode `1777`, so the installer can write guest temporary files. It then shuts down Darling before replacing the paired runtime: first exit the game and both clients manually.

The normal launcher starts neither an Electron inspector nor client API/UI helpers. It retains the existing runtime libraries and compatibility shims, disables incompatible Chromium features, supplies an absolute log directory, and selects the League product. The first-time installer uses `--skip-to-install` and omits the installed client's `--app-root`, avoiding an immediate exit or inaccessible intro screen. Legacy inspector hooks, API helpers, input automation and client binary patches remain historical tools; do not use them in the manual workflow. Keep account data, logs and captures private.

## Updating an existing runtime

These commands assume the pinned, patched source tree and its configured Makefiles already exist. From the repository root, build the two components with one worker to reduce resource contention:

```sh
cmake --build build/darling-src/build --target darlingserver system_kernel --parallel 1
sudo python3 tools/install-riot-darling-server.py
python3 tools/install-riot-memory-runtime.py
```

These two Python installers derive paths from the checkout/configuration and may be run directly from `tools/`; tokenized framework builders still require their rendered copies. The server installer defaults to `/usr/local/bin/darlingserver`, saves a checksum-named backup under `build/backups/darlingserver`, and preserves active server mappings. The memory installer updates the guest `RuntimeLibraries` and per-prefix bootstrap copy of `libsystem_kernel.dylib`, plus `build/riot-libsystem_kernel.dylib` if present. It leaves the system SDK alone. Each replacement is staged and verified before installation; the memory installer prepares every destination before replacing any. Replacement is atomic per file, not a transaction across all files.

Memory backups default to **`build/memory-runtime-backups`**, separate from the potentially root-owned directory created by the sudo server installer. If a custom backup directory is unwritable, choose another with `--backup-dir /path/to/writable/backups`. See [permission troubleshooting](TROUBLESHOOTING.md#memory-installer-permission-denied). Neither installer stops or starts any process.

An on-disk update is not activation. Existing processes keep their old executable/library mappings, and Darling's active overlay can expose an old library even to a new guest process. After manually exiting the game and both Riot/League clients, restart the entire namespace:

```sh
darling shutdown
sh scripts/run.sh
```

`darling shutdown` terminates the guest namespace; never run it during a match or while applications have work to save. Reopening only the client is insufficient. `scripts/run.sh` renders the current launcher templates for the next launch. No FPS/RAM improvement has yet been established for these incremental changes; see [status](STATUS.md) and [standalone checks](TESTING.md).

For GL resolvers, multi-window presentation and the LaunchServices crash fix, use the full paired installation from the README after exiting clients. It rebuilds the native GL wrapper and installs both GL library copies, as well as the LaunchServices absolute subframework path used by Electron. Copying only the OpenGL framework or only the flat LaunchServices override leaves an old library reachable.

Build/install steps need substantial time and storage. All 29 component patches were checked against their pinned base indexes. The runtime has been exercised on the new RX 6600 PC, but an entirely fresh checkout/build/install remains to be validated on another machine; patch applicability alone does not establish distro/driver support. Use `JOBS=2 ./scripts/build.sh` if the default `nproc` parallelism exceeds your available RAM. See [troubleshooting](TROUBLESHOOTING.md) before changing compiler flags or prefix ownership.

## Committing from a new PC

Building does not require a Git author identity, but committing does. If Git reports `Author identity unknown`, configure your own name and email in this repository (use your GitHub noreply address if desired):

```sh
git config user.name "Your Name"
git config user.email "your-commit-email"
```

Omitting `--global` keeps these settings local to this checkout. Commit source patches, tools and docs; keep `build/`, binaries, backups, captures and account data ignored.
