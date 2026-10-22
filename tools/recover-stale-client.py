#!/usr/bin/env python3
"""Partially recover a live Darling runtime with a stale client tree.

This is opt-in through RCLOL_RECOVER_STALE_CLIENT=1. It only acts when all
of these are true:

- a darlingserver is running;
- no League game process is running;
- no managed Riot/League client window is visible;
- one or more Riot/League client, CEF or crash-handler processes are still
  present for this prefix.

It terminates only those client processes with SIGTERM/SIGKILL. The
darlingserver, launchd/mldr services, sockets and .init.pid stay intact so the
next launcher run can reuse the runtime. It never patches files or the game.
"""

import os
import signal
import subprocess
import sys
import time
from pathlib import Path

GAME_NAME = 'LeagueofLegends'
WINDOW_CLASSES = ('leagueclientux', 'riot client')
CLIENT_MARKERS = (
    '/Applications/League of Legends.app',
    '/Applications/Riot Client.app',
    '/Users/Shared/Riot Games/Riot Client.app',
    'RiotClientServices',
    'Riot Client',
    'LeagueClient',
    'LeagueClientUx',
    'LeagueCrashHandler',
)
RUNTIME_NAMES = ('darlingserver', 'darling')
SOCK_VARIABLE = b'__mldr_sockpath'
SOCK_NAME = '.darlingserver.sock'


def target_prefix():
    value = os.environ.get('DPREFIX', str(Path.home() / '.darling'))
    prefix = Path(value).expanduser()
    return prefix if prefix.is_absolute() else None


def command_names(proc):
    try:
        raw = (proc / 'cmdline').read_bytes()
    except OSError:
        return []
    return [
        os.path.basename(part.decode(errors='replace'))
        for part in raw.split(b'\0')
        if part
    ]


def command_text(proc):
    try:
        raw = (proc / 'cmdline').read_bytes()
    except OSError:
        return ''
    return b' '.join(part for part in raw.split(b'\0') if part).decode(
        errors='replace')


def process_state(proc):
    try:
        return (proc / 'stat').read_text().rsplit(')', 1)[1].split()[0]
    except (OSError, IndexError):
        return None


def server_running(proc_root=Path('/proc')):
    try:
        entries = list(proc_root.iterdir())
    except OSError:
        return False
    for proc in entries:
        if not proc.name.isdigit():
            continue
        try:
            if (proc / 'comm').read_text().strip() == 'darlingserver':
                return True
        except OSError:
            pass
    return False


def game_running(proc_root=Path('/proc')):
    for proc in proc_root.iterdir() if proc_root.exists() else ():
        if not proc.name.isdigit():
            continue
        names = command_names(proc)
        if GAME_NAME in names:
            return True
    return False


def visible_client_window():
    """Return True when a managed client window is present.

    A missing xprop or display is treated as 'cannot prove there is no
    window' so we do not kill a possibly working client.
    """
    try:
        listing = subprocess.check_output(
            ['xprop', '-root', '_NET_CLIENT_LIST'], text=True, timeout=5
        )
    except (OSError, subprocess.SubprocessError):
        return True
    for raw in listing.split():
        if not raw.startswith('0x'):
            continue
        try:
            classes = subprocess.check_output(
                ['xprop', '-id', raw, 'WM_CLASS'], text=True, timeout=5
            ).lower()
        except (OSError, subprocess.SubprocessError):
            return True
        if any(name in classes for name in WINDOW_CLASSES):
            return True
    return False


def client_pids(prefix, proc_root=Path('/proc')):
    """Client/CEF PIDs belonging to prefix, excluding Darling and the game."""
    owner = (prefix / SOCK_NAME).parent.resolve()
    found = []
    try:
        entries = list(proc_root.iterdir())
    except OSError:
        return []
    for proc in entries:
        if not proc.name.isdigit() or process_state(proc) in ('Z', None):
            continue
        try:
            if proc.stat().st_uid != os.getuid():
                continue
            names = command_names(proc)
            if GAME_NAME in names or any(name in RUNTIME_NAMES for name in names):
                continue
            text = command_text(proc)
            if not any(marker in text for marker in CLIENT_MARKERS):
                continue
            try:
                env = dict(
                    item.split(b'=', 1)
                    for item in (proc / 'environ').read_bytes().split(b'\0')
                    if b'=' in item
                )
            except OSError:
                continue
            socket = env.get(SOCK_VARIABLE)
            if socket is None:
                continue
            if Path(os.fsdecode(socket)).parent.resolve() != owner:
                continue
            found.append(int(proc.name))
        except (OSError, ValueError):
            continue
    return sorted(found)


def recover(prefix, proc_root=Path('/proc'), window_check=visible_client_window,
            kill=os.kill, sleep=time.sleep, out=sys.stdout):
    if not server_running(proc_root):
        print('recover-stale-client: no darlingserver; normal startup', file=out, flush=True)
        return 0
    if game_running(proc_root):
        print('recover-stale-client: game running; no action', file=out, flush=True)
        return 0
    if window_check():
        print('recover-stale-client: client window present; no action', file=out, flush=True)
        return 0
    pids = client_pids(prefix, proc_root)
    if not pids:
        print('recover-stale-client: no stale client processes; no action', file=out, flush=True)
        return 0
    print('recover-stale-client: stale client tree without a window; '
          'terminating %d process(es)' % len(pids), file=out, flush=True)
    for pid in pids:
        try:
            kill(pid, signal.SIGTERM)
        except OSError:
            pass
    sleep(4)
    for pid in pids:
        try:
            kill(pid, signal.SIGKILL)
        except OSError:
            pass
    return 0


def main():
    if os.environ.get('RCLOL_RECOVER_STALE_CLIENT') != '1':
        print('recover-stale-client: disabled; set RCLOL_RECOVER_STALE_CLIENT=1',
              flush=True)
        return 0
    prefix = target_prefix()
    if prefix is None:
        print('recover-stale-client: unusable DPREFIX; no action',
              file=sys.stderr, flush=True)
        return 1
    return recover(prefix)


if __name__ == '__main__':
    sys.exit(main())
