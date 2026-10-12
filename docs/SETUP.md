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

The runtime installer requires native `pkg-config` entries for `vpx`, `x11`, `x11-xcb`, `xcb-shm` and `xext`, plus the base Darling SDK. Some bridge guards intentionally fall back to Chromium's own implementation when the installed client changes.

The launcher enables loopback-only Electron inspection on `127.0.0.1:9229` for its startup compatibility hooks. It reads local Riot API credentials from the running client when necessary; no credentials are stored in this repository. Keep logs and local captures private.

Build/install steps need substantial time and storage. The patch set was checked against all pinned base indexes on the development machine. An entirely fresh build and a game launch on the second PC remain to be tested; do not interpret patch applicability as proof that every distro/driver works.
