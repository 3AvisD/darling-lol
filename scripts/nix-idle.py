#!/usr/bin/env python3
"""Read-only, fail-closed rootless maintenance preflight. Never signal a process."""
import os
from pathlib import Path


def client_active(prefix, *, processes=None):
    """Check exact-prefix mappings; only vanished processes may be skipped."""
    paths = [str(prefix / name) + '/' for name in (
        'Applications/Riot Client.app', 'Applications/League of Legends.app',
        'Users/Shared/Riot Games')]
    namespace_map = Path('/proc/self/uid_map').read_text()
    for process in Path('/proc').iterdir() if processes is None else processes:
        if not process.name.isdigit():
            continue
        try:
            if process.stat().st_uid != os.getuid():
                continue
            # The outer live-marker gate excludes earlier Darling namespaces.
            # Host processes are not ours to inspect; their maps are commonly
            # unreadable from a child user namespace (including systemd --user).
            if processes is None and (process / 'uid_map').read_text() != namespace_map:
                continue
            mappings = (process / 'maps').read_text()
            if any(path in mappings for path in paths):
                return True
        except (FileNotFoundError, ProcessLookupError):
            # Process exited between enumeration and inspection.
            continue
        except OSError as error:
            raise RuntimeError(f'Cannot inspect process {process.name}; cannot establish that the prefix is idle: {error}') from error
    return False


def main():
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
        except (FileNotFoundError, ProcessLookupError):
            pass
        except OSError as error:
            raise SystemExit(f'Cannot validate prefix server marker: {error}') from error
    try:
        active = client_active(prefix)
    except (OSError, RuntimeError) as error:
        raise SystemExit(str(error)) from error
    if active:
        raise SystemExit('A client maps this prefix; exit it before rootless maintenance. No process was stopped.')


if __name__ == '__main__':
    main()
