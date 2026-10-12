#!/usr/bin/env python3
"""Clear only a dead Darling server's stale PID marker before startup."""
import os
from pathlib import Path
import time
prefix = Path(os.environ.get('DPREFIX', str(Path.home() / '.darling')))
marker = prefix / '.init.pid'
try:
    pid = int(marker.read_text().strip())
except (OSError, ValueError):
    raise SystemExit(0)
if pid <= 0:
    raise SystemExit(0)
status = Path('/proc') / str(pid) / 'status'
try:
    state = next(line.split()[1] for line in status.read_text().splitlines() if line.startswith('State:'))
    dead = state == 'Z'
except FileNotFoundError:
    dead = True
except (OSError, StopIteration):
    dead = False
if dead:
    os.replace(marker, prefix / f'.init.pid.stale-{pid}-{time.time_ns()}')
    print(f'Recovered stale Darling server marker for PID {pid}', flush=True)
