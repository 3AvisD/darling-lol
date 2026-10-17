#!/bin/sh
# Builds and installs the paired per-user runtime; does not copy game files.
set -eu
repo=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$repo"
python3 scripts/configure.py
if [ -d build/venv/bin ]; then PATH="$repo/build/venv/bin:$PATH"; export PATH; fi
# A server killed by the base install leaves a stale .init.pid that makes darling shell fail.
python3 build/runtime/tools/ensure-darling-runtime.py
# Initialize the normal user-owned prefix before installing overrides.
darling shell /usr/bin/true
# The prefix has no /private/tmp of its own, so the guest falls through to the root-owned
# installed layer and every write there (the Riot installer's temp files) fails with EACCES.
mkdir -p "$HOME/.darling/private/tmp"
chmod 1777 "$HOME/.darling/private/tmp"
# Build the system root store now that the prefix exists; Darling is stopped next anyway.
python3 build/runtime/tools/ensure-darling-root-certificates.py ||
  echo 'Darling system root store not installed; the launcher retries it' >&2
python3 build/runtime/tools/stop-darling-runtime.py
sh build/runtime/tools/install-riot-presentation-runtime.sh
sh build/runtime/tools/install-riot-gpu-runtime.sh
# darling-lol perf: build and install optimized libc/libc++/libplatform.
if [ -f build/runtime/tools/build-riot-libs-runtime.sh ]; then
  sh build/runtime/tools/build-riot-libs-runtime.sh
  python3 build/runtime/tools/install-riot-libs-runtime.py
fi
python3 build/runtime/tools/install-darling-system-profiler.py
# Every guest process loads these overlays; refuse a set the compiler broke (check-darwin-tlv-calls.py).
find "$HOME/.darling/Users/$(id -un)/RuntimeLibraries" "$HOME/.darling/Users/$(id -un)/RuntimeFrameworks" \
  -type f ! -name '*.before-*' ! -name '*.rt-*' ! -name '*.llvm*' -size +4k -print0 |
  xargs -0 python3 build/runtime/tools/check-darwin-tlv-calls.py
