#!/bin/sh
set -eu
repo=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$repo"
python3 scripts/configure.py >/dev/null
if [ -d build/venv/bin ]; then PATH="$repo/build/venv/bin:$PATH"; export PATH; fi
# darling-lol perf: optionally wrap with gamemoderun for automatic
# CPU governor, nice level, and scheduler optimizations.
if [ "${RCLOL_GAMEMODE:-auto}" != "0" ] && command -v gamemoderun >/dev/null 2>&1; then
  exec gamemoderun sh build/runtime/tools/run-riot-client.sh
fi
exec sh build/runtime/tools/run-riot-client.sh
