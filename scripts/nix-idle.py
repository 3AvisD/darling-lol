#!/usr/bin/env python3
"""Read-only, fail-closed rootless maintenance preflight. Never signal a process."""
import os
from pathlib import Path

prefix = Path(os.environ.get('DPREFIX', str(Path.home() / '.darling'))).resolve()
try:
    pid = int((prefix / '.init.pid').read_text().strip())
except FileNotFoundError:
    pid = 0
except (OSError, ValueError):
    raise SystemExit('Cannot validate prefix server marker; inspect it before maintenance.')
if pid > 0:
    try:
        status = (Path('/proc') / str(pid) / 'status').read_text()
        if not any(line.startswith('State:') and line.split()[1] == 'Z' for line in status.splitlines()):
            raise SystemExit('Prefix has a live server marker; exit Riot/League and run darling shutdown in the ORIGINAL namespace before updating. No process was stopped.')
    except FileNotFoundError:
        pass
# Only inspect same-user mappings under this exact prefix. An unrelated game's
# command name is not grounds to stop or modify it. Unreadable same-user maps
# fail closed because we cannot rule out a client still using these files.
paths = [str(prefix / name) + '/' for name in (
    'Applications/Riot Client.app', 'Applications/League of Legends.app',
    'Users/Shared/Riot Games')]
for process in Path('/proc').iterdir():
    if not process.name.isdigit():
        continue
    try:
        if process.stat().st_uid != os.getuid():
            continue
        mappings = (process / 'maps').read_text()
        if any(path in mappings for path in paths):
            raise SystemExit('A client maps this prefix; exit it before rootless maintenance. No process was stopped.')
    except (FileNotFoundError, ProcessLookupError, PermissionError):
        continue
