#!/usr/bin/env python3
"""darling-lol --stop: end the workspace's session and its Darling server.

The session shuts Darling down in its own namespace when it finds the stop
request. Only if it does not finish in time, or if no session holds the lock
but the prefix server is still alive, is that server killed from here. The
marker PID alone is not trusted: the process must be a darlingserver of this
user for this prefix.
"""
import fcntl
import os
from pathlib import Path
import signal
import sys
import time

WAIT_SECONDS = 30


def session_running(lock):
    try:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
    except BlockingIOError:
        return True
    fcntl.flock(lock, fcntl.LOCK_UN)
    return False


def wait_for_session(lock):
    deadline = time.monotonic() + WAIT_SECONDS
    while session_running(lock):
        if time.monotonic() > deadline:
            return False
        time.sleep(0.5)
    return True


def prefix_server(prefix):
    """PID of this prefix's darlingserver, or 0 if none is running."""
    try:
        pid = int((prefix / '.init.pid').read_text().strip())
    except (FileNotFoundError, ValueError):
        return 0
    process = Path('/proc') / str(pid)
    try:
        argv = (process / 'cmdline').read_bytes().split(b'\0')
        owner = process.stat().st_uid
    except (FileNotFoundError, ProcessLookupError):
        return 0
    if (pid <= 0 or owner != os.getuid() or len(argv) < 2
            or os.path.basename(argv[0]) != b'darlingserver'
            or Path(os.fsdecode(argv[1])).resolve() != prefix):
        return 0
    return pid


def kill_server(pid):
    """Same as darling shutdown: kill launchd, which ends its PID namespace, then the server."""
    children = []
    for task in (Path('/proc') / str(pid) / 'task').glob('*/children'):
        try:
            children += [int(child) for child in task.read_text().split()]
        except (FileNotFoundError, ProcessLookupError):
            pass
    for target in [*children, pid]:
        try:
            os.kill(target, signal.SIGKILL)
        except ProcessLookupError:
            pass


def main():
    workspace, prefix = Path(sys.argv[1]), Path(sys.argv[2]).resolve()
    request = workspace / '.stop-request'
    workspace.mkdir(parents=True, exist_ok=True)
    with open(workspace / '.launcher.lock', 'a+') as lock:
        if session_running(lock):
            print('Asking the darling-lol session to shut Darling down...', flush=True)
            request.touch()
            if wait_for_session(lock):
                request.unlink(missing_ok=True)
                print('Stopped.')
                return 0
            print(f'The session did not stop within {WAIT_SECONDS} seconds.', flush=True)
        server = prefix_server(prefix)
        if server:
            print(f'Killing the Darling server of {prefix} (PID {server}).', flush=True)
            kill_server(server)
        elif not session_running(lock):
            print('No darling-lol session is running.')
            return 0
        if wait_for_session(lock):
            request.unlink(missing_ok=True)
            print('Stopped.')
            return 0
        # Do not leave a request that would stop the next session at once.
        request.unlink(missing_ok=True)
        print('Darling is stopped, but the session that started it is still running. '
              'Close its terminal.', file=sys.stderr)
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
