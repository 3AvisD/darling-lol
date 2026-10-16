#!/usr/bin/env python3
"""Clear only a dead Darling server's stale PID marker and buffers before startup."""
import os
from pathlib import Path
import subprocess
import time

prefix = Path(os.environ.get('DPREFIX', str(Path.home() / '.darling')))
marker = prefix / '.init.pid'

server_running = False
if marker.exists():
    try:
        pid = int(marker.read_text().strip())
        if pid > 0:
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
            else:
                server_running = True
    except (OSError, ValueError):
        pass

# Only clean up stale Riot temporary files and shared memory frames if the server is NOT running
if not server_running:
    try:
        subprocess.run(['killall', '-q', 'chrome_crashpad_handler'], check=False)
    except Exception:
        pass

    try:
        tmp_dir = prefix / 'private/tmp'
        if tmp_dir.is_dir():
            for f in tmp_dir.glob('.com.riotgames.*'):
                try:
                    f.unlink(missing_ok=True)
                except OSError:
                    pass
    except OSError:
        pass

    try:
        shm_dir = Path('/dev/shm')
        if shm_dir.is_dir():
            for f in shm_dir.glob('darling-*'):
                try:
                    f.unlink(missing_ok=True)
                except OSError:
                    pass
    except OSError:
        pass
