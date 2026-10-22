#!/usr/bin/env python3
"""Opt-in: hide the League client UI while a match is running.

`RCLOL_HIDE_CLIENT_IN_GAME=1` is an alternative to closing the client UI with
`RCLOL_CLOSE_CLIENT_IN_GAME=1`. It hides the managed X11 windows for
`LeagueClientUx` and `Riot Client`, and shows them again when the game process
exits. The Riot client, League backend, CEF helpers and game keep running.

This is launcher/window management only. It does not patch client binaries,
CEF, anti-cheat or DRM, and it never hides the game window. It is off by
default, fails closed, and is intended for X11/Xwayland desktops.

The game is attributed to this prefix through the loader's
`__mldr_sockpath`, because guest processes do not carry `DPREFIX`. If proc,
ownership or window information cannot be read, no window is touched.

Diagnose one observation with:

    python3 tools/hide-league-client-in-game.py --once

Enable it for a launcher run with:

    RCLOL_HIDE_CLIENT_IN_GAME=1 ./scripts/run.sh
"""

import argparse
import ctypes
import os
import re
import signal
import subprocess
import sys
import time
from pathlib import Path

GAME_NAME = 'LeagueofLegends'
SOCK_VARIABLE = b'__mldr_sockpath'
SOCK_NAME = '.darlingserver.sock'
POLL_SECONDS = 2.0
TARGET_CLASSES = ('leagueclientux', 'riot client')


def target_prefix():
    """The prefix this launcher owns, or None when it cannot be determined."""
    value = os.environ.get('DPREFIX', str(Path.home() / '.darling'))
    prefix = Path(value).expanduser()
    return prefix if prefix.is_absolute() else None


def process_state(proc):
    try:
        return (proc / 'stat').read_text().rsplit(')', 1)[1].split()[0]
    except (OSError, IndexError):
        return None


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


def game_running(prefix, proc_root=Path('/proc')):
    """True/False for a game of this prefix; None when /proc is unreliable.

    The game executable is matched by its exact file name. The process must
    carry the loader socket of this prefix, so a game from another install is
    never considered. Any unreadable candidate makes the result None (fail
    closed).
    """
    owner = (prefix / SOCK_NAME).parent.resolve()
    try:
        entries = list(proc_root.iterdir())
    except OSError:
        return None
    for proc in entries:
        if not proc.name.isdigit() or process_state(proc) in ('Z', None):
            continue
        try:
            if proc.stat().st_uid != os.getuid():
                continue
            if GAME_NAME not in command_names(proc):
                continue
            environment = dict(
                item.split(b'=', 1)
                for item in (proc / 'environ').read_bytes().split(b'\0')
                if b'=' in item
            )
            socket = environment.get(SOCK_VARIABLE)
            if socket is None:
                continue
            if Path(os.fsdecode(socket)).parent.resolve() != owner:
                continue
            return True
        except FileNotFoundError:
            if proc.exists():
                return None
            continue
        except (OSError, ValueError):
            return None
    return False


def xprop_root():
    try:
        listing = subprocess.check_output(
            ['xprop', '-root', '_NET_CLIENT_LIST'], text=True, timeout=5
        )
    except (OSError, subprocess.SubprocessError):
        return []
    return re.findall(r'0x[0-9a-fA-F]+', listing)


def window_class(window):
    try:
        prop = subprocess.check_output(
            ['xprop', '-id', window, 'WM_CLASS'], text=True, timeout=5
        )
    except (OSError, subprocess.SubprocessError):
        return ''
    return prop.lower()


def target_windows():
    """Managed client windows only; the game window is never selected."""
    found = []
    for window in xprop_root():
        classes = window_class(window)
        if any(name in classes for name in TARGET_CLASSES):
            found.append(int(window, 16))
    return sorted(set(found))


class X11:
    def __init__(self):
        self.lib = ctypes.CDLL('libX11.so.6')
        self.lib.XOpenDisplay.restype = ctypes.c_void_p
        self.lib.XOpenDisplay.argtypes = [ctypes.c_char_p]
        self.lib.XUnmapWindow.argtypes = [ctypes.c_void_p, ctypes.c_ulong]
        self.lib.XMapRaised.argtypes = [ctypes.c_void_p, ctypes.c_ulong]
        self.lib.XSync.argtypes = [ctypes.c_void_p, ctypes.c_int]
        self.lib.XCloseDisplay.argtypes = [ctypes.c_void_p]
        error_type = ctypes.CFUNCTYPE(ctypes.c_int, ctypes.c_void_p, ctypes.c_void_p)
        self._error_handler = error_type(lambda _display, _event: 0)
        self.lib.XSetErrorHandler.argtypes = [error_type]
        self.lib.XSetErrorHandler.restype = ctypes.c_void_p
        self.lib.XSetErrorHandler(self._error_handler)
        self.display = self.lib.XOpenDisplay(None)
        if not self.display:
            raise RuntimeError('cannot open DISPLAY')

    def unmap(self, window):
        self.lib.XUnmapWindow(self.display, window)
        self.lib.XSync(self.display, 0)

    def show(self, window):
        self.lib.XMapRaised(self.display, window)
        self.lib.XSync(self.display, 0)

    def close(self):
        self.lib.XCloseDisplay(self.display)


class Watcher:
    """Hide while a match is running, restore on end, uncertainty or exit."""

    def __init__(self, prefix, x11, window_source=target_windows,
                 state_source=game_running, sleep=time.sleep, out=sys.stdout):
        self.prefix = prefix
        self.x11 = x11
        self.window_source = window_source
        self.state_source = state_source
        self.sleep = sleep
        self.out = out
        self.hidden = set()
        self.reported_no_window = False

    def _show_all(self, reason):
        if not self.hidden:
            return
        print(f'hide-league-client-in-game: {reason}; showing the client', file=self.out, flush=True)
        for window in sorted(self.hidden):
            try:
                self.x11.show(window)
                print(f'hide-league-client-in-game: shown 0x{window:x}', file=self.out, flush=True)
            except Exception as error:
                print(f'hide-league-client-in-game: cannot show 0x{window:x}: {error}',
                      file=sys.stderr, flush=True)
        self.hidden.clear()

    def _hide_new(self):
        windows = self.window_source()
        if not windows and not self.hidden and not self.reported_no_window:
            print('hide-league-client-in-game: no managed client window; not hiding',
                  file=self.out, flush=True)
            self.reported_no_window = True
        for window in windows:
            if window in self.hidden:
                continue
            try:
                self.x11.unmap(window)
                self.hidden.add(window)
                print(f'hide-league-client-in-game: hidden 0x{window:x}', file=self.out, flush=True)
            except Exception as error:
                print(f'hide-league-client-in-game: cannot hide 0x{window:x}: {error}',
                      file=sys.stderr, flush=True)

    def run(self, max_steps=None):
        steps = 0
        while True:
            state = self.state_source(self.prefix)
            if state is None:
                self._show_all('unreadable /proc state')
            elif state:
                if not self.hidden and self.reported_no_window:
                    self.reported_no_window = False
                self._hide_new()
            else:
                self._show_all('the match ended')
                self.reported_no_window = False
            steps += 1
            if max_steps is not None and steps >= max_steps:
                return 0
            self.sleep(POLL_SECONDS)

    def restore(self):
        self._show_all('exiting')


def main(argv=None):
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--detach', action='store_true',
                        help='leave the launcher session so a terminal hang-up cannot end it')
    parser.add_argument('--once', action='store_true',
                        help='one game observation and exit (no window changes)')
    args = parser.parse_args(argv)

    if os.environ.get('RCLOL_CLOSE_CLIENT_IN_GAME') == '1':
        print('hide-league-client-in-game: ignored; RCLOL_CLOSE_CLIENT_IN_GAME=1 takes precedence',
              file=sys.stderr, flush=True)
        return 0
    if os.environ.get('RCLOL_HIDE_CLIENT_IN_GAME') != '1':
        print('hide-league-client-in-game: disabled; set RCLOL_HIDE_CLIENT_IN_GAME=1',
              flush=True)
        return 0
    prefix = target_prefix()
    if prefix is None:
        print('hide-league-client-in-game: unusable DPREFIX: no action',
              file=sys.stderr, flush=True)
        return 1
    if args.once:
        state = game_running(prefix)
        print(f'hide-league-client-in-game: match={"unreadable" if state is None else "running" if state else "absent"}',
              flush=True)
        return 0
    if args.detach:
        try:
            os.setsid()
        except OSError:
            pass
    try:
        x11 = X11()
    except Exception as error:
        print(f'hide-league-client-in-game: cannot open X11: {error}',
              file=sys.stderr, flush=True)
        return 1

    watcher = Watcher(prefix, x11)

    def stop(_signum, _frame):
        raise KeyboardInterrupt

    signal.signal(signal.SIGTERM, stop)
    signal.signal(signal.SIGINT, stop)
    print('hide-league-client-in-game: watching %s for a running match; '
          'opt out with RCLOL_HIDE_CLIENT_IN_GAME=0' % prefix, flush=True)
    try:
        watcher.run()
    except KeyboardInterrupt:
        pass
    finally:
        watcher.restore()
        x11.close()
    return 0


if __name__ == '__main__':
    sys.exit(main())
