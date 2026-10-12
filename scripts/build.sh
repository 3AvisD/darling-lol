#!/bin/sh
set -eu
repo=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$repo"
python3 scripts/configure.py
if [ -d build/venv/bin ]; then PATH="$repo/build/venv/bin:$PATH"; export PATH; fi
python3 scripts/prepare.py
cmake -S build/darling-src -B build/darling-src/build -G 'Unix Makefiles' \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr/local -DENABLE_METAL=ON
cmake --build build/darling-src/build --parallel "${JOBS:-$(nproc)}"
echo 'Build complete. Install the base runtime with: sudo cmake --install build/darling-src/build'
