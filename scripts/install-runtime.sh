#!/bin/sh
# Builds and installs the paired per-user runtime; does not copy game files.
set -eu
repo=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$repo"
python3 scripts/configure.py
if [ -d build/venv/bin ]; then PATH="$repo/build/venv/bin:$PATH"; export PATH; fi
# Initialize the normal user-owned prefix before installing overrides.
darling shell /usr/bin/true
python3 build/runtime/tools/stop-darling-runtime.py
sh build/runtime/tools/install-riot-presentation-runtime.sh
sh build/runtime/tools/install-riot-gpu-runtime.sh
python3 build/runtime/tools/install-darling-system-profiler.py
python3 scripts/install-shim.py
