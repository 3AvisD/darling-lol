#!/usr/bin/env python3
"""Clear a dead Darling server's stale PID marker, and stale shared buffers once no server runs."""
import os
from pathlib import Path
import signal
import time
def recover_marker(prefix):
    marker = prefix / '.init.pid'
    try:
        pid = int(marker.read_text().strip())
    except (OSError, ValueError):
        pid = 0
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

def any_server_running(proc_root=Path('/proc')):
    """True if any darlingserver runs, for any prefix: /dev/shm buffers are not per prefix."""
    for proc in proc_root.iterdir():
        if not proc.name.isdigit():
            continue
        try:
            if (proc / 'comm').read_text().strip() == 'darlingserver':
                return True
        except PermissionError:
            # A hidden process might be a server; shared buffers are global, so fail closed.
            return True
        except OSError:
            pass
    return False

def stop_orphaned_launchd(proc_root=Path('/proc')):
    """A guest launchd that outlived its darlingserver cannot be shut down with `darling shutdown`
    and makes the base install (tools/shutdown-user.sh) refuse to continue."""
    uid = os.getuid()
    for proc in proc_root.iterdir():
        if not proc.name.isdigit():
            continue
        try:
            if proc.stat().st_uid != uid or (proc / 'cmdline').read_bytes().rstrip(b'\0') != b'/sbin/launchd':
                continue
            os.kill(int(proc.name), signal.SIGKILL)
            print(f'Stopped orphaned Darling launchd PID {proc.name}', flush=True)
        except (OSError, ValueError):
            pass

def stop_riot_crashpad(proc_root=Path('/proc')):
    """A Riot crash handler outliving its server holds the old session's ports; spare every other app's."""
    uid = os.getuid()
    for proc in proc_root.iterdir():
        if not proc.name.isdigit():
            continue
        try:
            if not (proc / 'comm').read_text().startswith('chrome_crashpad') or proc.stat().st_uid != uid:
                continue
            argv = (proc / 'cmdline').read_bytes().split(b'\0')
            executable = argv[0].decode(errors='replace')
            # Match the executable's bundle, not arbitrary arguments (a browser may mention Riot).
            if any(bundle in executable for bundle in (
                '/Riot Client.app/', '/League of Legends.app/', '/League of Legends (TM) Client.app/'
            )):
                os.kill(int(proc.name), signal.SIGTERM)
        except (OSError, ValueError):
            pass

def remove_owned(paths):
    uid = os.getuid()
    for path in paths:
        try:
            if path.is_file() and path.stat().st_uid == uid:
                path.unlink()
        except OSError:
            pass

def main():
    prefix = Path(os.environ.get('DPREFIX', str(Path.home() / '.darling')))
    recover_marker(prefix)
    # Frame and present buffers left by a killed session confuse the next one's publishers.
    if not any_server_running():
        stop_orphaned_launchd()
        stop_riot_crashpad()
        remove_owned((prefix / 'private/tmp').glob('.com.riotgames.*'))
        remove_owned(Path('/dev/shm').glob('darling-*'))


if __name__ == '__main__':
    main()
