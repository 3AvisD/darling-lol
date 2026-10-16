#!/bin/sh
# Builds and installs the paired per-user runtime; does not copy game files.
set -eu
repo=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$repo"
python3 scripts/configure.py
if [ -d build/venv/bin ]; then PATH="$repo/build/venv/bin:$PATH"; export PATH; fi
# Initialize the normal user-owned prefix before installing overrides.
darling shell /usr/bin/true
# The prefix has no /private/tmp of its own, so the guest falls through to the root-owned
# installed layer and every write there (the Riot installer's temp files) fails with EACCES.
mkdir -p "$HOME/.darling/private/tmp"
chmod 1777 "$HOME/.darling/private/tmp"
python3 build/runtime/tools/stop-darling-runtime.py
sh build/runtime/tools/install-riot-presentation-runtime.sh
sh build/runtime/tools/install-riot-gpu-runtime.sh
# darling-lol perf: build and install optimized libc/libc++/libplatform.
if [ -f build/runtime/tools/build-riot-libs-runtime.sh ]; then
  sh build/runtime/tools/build-riot-libs-runtime.sh
  python3 build/runtime/tools/install-riot-libs-runtime.py
fi
python3 build/runtime/tools/install-darling-system-profiler.py
# darling-lol: install GNOME application launcher and icon.
if [ -f build/runtime/tools/riot-desktop-entry.py ]; then
  python3 build/runtime/tools/riot-desktop-entry.py || true
fi
