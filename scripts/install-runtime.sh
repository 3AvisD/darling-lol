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
python3 build/runtime/tools/install-darling-system-profiler.py
python3 scripts/install-shim.py
