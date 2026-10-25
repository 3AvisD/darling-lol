#!/usr/bin/env python3
"""Keep namespace-local shutdown authority until the foreground and clients exit.

Never kill by name or use a marker PID as authority to signal a host process.
Uninspectable mappings retain this namespace rather than guessing it is idle.
"""
import importlib.util
import os
from pathlib import Path
import signal
import subprocess
import sys
import time


def warn(message):
    """Diagnostics must not discard the namespace after a terminal hangup."""
    try:
        print(message, file=sys.stderr, flush=True)
    except OSError:
        pass


def main():
    scripts = Path(__file__).resolve().parent
    spec = importlib.util.spec_from_file_location('nix_idle', scripts / 'nix-idle.py')
    assert spec is not None and spec.loader is not None
    idle = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(idle)
    prefix = Path(os.environ['DPREFIX']).resolve()
    # darling-lol --stop creates this file; the launcher removes it.
    stop_request = Path(os.environ['DARLING_WORKSPACE']) / '.stop-request'
    child = None
    interrupted = 0

    def stopping():
        return interrupted or stop_request.exists()

    def interrupted_by(signum, _frame):
        nonlocal interrupted
        interrupted = 128 + signum
        # Only our own foreground child, never a marker PID or host-wide search.
        if child is not None and child.poll() is None:
            try:
                child.send_signal(signum)
            except ProcessLookupError:
                pass

    for signum in (signal.SIGINT, signal.SIGTERM, signal.SIGHUP):
        signal.signal(signum, interrupted_by)
    result = 1
    try:
        child = subprocess.Popen(sys.argv[1:])
        # Poll rather than wait: guest processes can outlive the command, and a
        # signal or stop request must still reach the shutdown below.
        while child.poll() is None and not stopping():
            time.sleep(0.5)
    except OSError as error:
        warn(f'Cannot start client: {error}')
    finally:
        # A launcher can return while guest clients remain. Do not tear down
        # their server, or discard the only namespace capable of shutting it down,
        # unless the user asked to stop.
        last_error = None
        while not stopping():
            try:
                if not idle.client_active(prefix):
                    break
                last_error = None
            except (OSError, RuntimeError) as error:
                if str(error) != last_error:
                    warn(f'{error}; retaining original namespace and retrying. '
                         'Exit clients to allow safe shutdown.')
                    last_error = str(error)
            time.sleep(1)
        if stopping():
            warn('Stopping Darling.')
        while True:
            try:
                shutdown = subprocess.run(['bash', str(scripts / 'darling-rootless.sh'), 'shutdown']).returncode
            except OSError as error:
                warn(f'Cannot run namespace-local shutdown: {error}')
                shutdown = 1
            if not shutdown:
                break
            try:
                # Nothing is left to shut down, for example after the server crashed.
                if not idle.server_running(prefix):
                    break
            except (OSError, ValueError):
                pass
            warn(f'Darling namespace-local shutdown failed (status {shutdown}); '
                 'retaining the original namespace and retrying in 5 seconds. '
                 'Keep this terminal open.')
            time.sleep(5)
        if child is not None:
            # Without its server the command exits; it is our own child to kill.
            try:
                result = child.wait(timeout=10)
            except subprocess.TimeoutExpired:
                child.kill()
                result = child.wait()
            if result < 0:
                result = 128 - result
        result = interrupted or result
    return result


if __name__ == '__main__':
    raise SystemExit(main())
