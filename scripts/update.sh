#!/bin/sh
# Bring an existing build and installation up to date after `git pull`: re-apply the patches that changed
# (prepare.py reverts the earlier version it finds applied), rebuild incrementally, and reinstall the base
# runtime and the per-user overrides. Run it with no Riot/League session open; it never stops one.
set -eu
repo=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$repo"
if [ ! -d build/darling-src/build ]; then
	echo 'No existing build; follow the first-time setup in README.md instead.' >&2
	exit 1
fi
# The install steps shut Darling down and stop leftover Riot processes, so refuse rather than end a session.
python3 - <<'PY'
from pathlib import Path
for entry in Path('/proc').iterdir():
    if not entry.name.isdigit():
        continue
    try:
        command_line = (entry / 'cmdline').read_bytes()
    except OSError:
        continue
    if b'/Riot Client.app/' in command_line or b'/League of Legends.app/' in command_line:
        raise SystemExit('A Riot Client or League process is running; exit the game and both clients, then run this again.')
PY
./scripts/build.sh
python3 build/runtime/tools/stop-darling-runtime.py
sudo cmake --install build/darling-src/build
./scripts/install-runtime.sh
echo 'Update installed. Start Darling with: sh scripts/run.sh'
