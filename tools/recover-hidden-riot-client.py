#!/usr/bin/env python3
"""Opt-in recovery of an old windowless Riot session on Hyprland; fail closed."""
import json
import math
import os
from pathlib import Path
import subprocess

MIN_AGE_SECONDS = 60


def recoverable_processes(proc_root, prefix):
    """Return stable process identities, or None when shutdown is not justified."""
    candidates = {}
    try:
        uptime = float((proc_root / 'uptime').read_text().split()[0])
        ticks = os.sysconf('SC_CLK_TCK')
        if not math.isfinite(uptime) or uptime < 0 or ticks <= 0:
            return None
        for proc in proc_root.iterdir():
            if not proc.name.isdigit():
                continue
            try:
                if proc.stat().st_uid != os.getuid():
                    continue
                executable = (proc / 'cmdline').read_bytes().split(b'\0')[0].decode(errors='replace')
                if executable.endswith('/LeagueofLegends'):
                    return None
                if '/Riot Client.app/' not in executable:
                    continue
                environment = dict(item.split(b'=', 1) for item in
                                   (proc / 'environ').read_bytes().split(b'\0') if b'=' in item)
                process_prefix = Path(os.fsdecode(environment.get(b'DPREFIX', os.fsencode(Path.home() / '.darling'))))
                if process_prefix != prefix:
                    continue
                # Field 22 follows a comm field that may contain spaces/parentheses.
                start_ticks = int((proc / 'stat').read_text().rsplit(')', 1)[1].split()[19])
                age = uptime - start_ticks / ticks
                if start_ticks < 0 or age < MIN_AGE_SECONDS:
                    return None  # One young client prevents restarting the entire prefix.
                candidates[proc.name] = start_ticks
            except FileNotFoundError:
                if proc.exists():
                    return None
                continue
    except (OSError, ValueError, IndexError):
        return None
    return candidates or None


def recover(proc_root=Path('/proc')):
    home = Path.home()
    prefix = Path(os.environ.get('DPREFIX', str(home / '.darling'))).expanduser()
    if prefix != home / '.darling':
        return False
    processes = recoverable_processes(proc_root, prefix)
    if not processes:
        return False
    try:
        result = subprocess.run(['hyprctl', 'clients', '-j'], check=True,
                                capture_output=True, text=True, timeout=5)
        windows = json.loads(result.stdout)
        if not isinstance(windows, list):
            return False
        for window in windows:
            if (not isinstance(window, dict) or
                    not all(isinstance(window.get(key), str) for key in ('class', 'title')) or
                    not all(type(window.get(key)) is bool for key in ('mapped', 'hidden'))):
                return False
            identity = (window['class'] + ' ' + window['title']).lower()
            if any(name in identity for name in ('riot', 'league')) and window['mapped'] and not window['hidden']:
                return False
    except (OSError, ValueError, subprocess.SubprocessError):
        return False
    # Compositor inspection can take seconds: refuse if a game/client started,
    # a PID was reused, or any relevant process disappeared during that interval.
    if recoverable_processes(proc_root, prefix) != processes:
        return False
    print('Recovering a Riot session with no visible client window.', flush=True)
    try:
        subprocess.run(['darling', 'shutdown'], check=True, timeout=25)
    except (OSError, subprocess.SubprocessError) as error:
        print(f'Riot session recovery failed: {error}', flush=True)
        return False
    return True


if __name__ == '__main__':
    recover()
