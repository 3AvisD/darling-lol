#!/usr/bin/env python3
"""Close the League client UI while a match runs; fail closed.

Opt-in helper for `run-riot-client.sh` (`RCLOL_CLOSE_CLIENT_IN_GAME=1`). The
League client keeps its Chromium/CEF UI (executable `LeagueClientUx` and its
helpers) alive during a match. Those processes compete with the game for CPU,
and the client the backend starts when the match ends is a second CEF instance
on the same `--user-data-dir` as the pre-match one, so its window never paints:
the DOM and audio work, the window stays black. Riot's own "Close client during
game" option has both outcomes on a Windows client, but under Darling it leaves
the pre-match instance running, so the option reproduces the black window.

This helper performs the close itself, from the host, while a match is running.
Only then is the client UI expendable, and the client that appears afterwards is
the only instance. It never touches the Riot client, the League backend
(`LeagueClient`) or the game, and it exits by itself when no client is left.

It is fail closed: it acts only after positively identifying a running game and
the League client UI of its own prefix, and never closes a client younger than
MIN_AGE_SECONDS (the one the backend starts after a match). An unreadable
`/proc` entry, an unreadable environment, an unreadable or malformed stat field,
a process of another install and a missing game all lead to no action.

The client UI is recognised by the name of its executable, not by the command
line: `LeagueCrashHandler` processes carry the `LeagueClientUx` string in their
arguments (attachment paths under `LeagueClientUx Logs`) and live under the same
`/League` paths, so a wider match would signal a related process.

Diagnose with `--once` (one observation) or `--once --dry-run` (report only).
"""
import argparse
import math
import os
from pathlib import Path
import signal
import sys
import time

UX_NAME = 'LeagueClientUx'
GAME_SUFFIX = '/LeagueofLegends'
# DPREFIX is not in the environment of guest processes (checked on a live session), but
# every process the loader starts carries the socket of the install it belongs to.
SOCK_VARIABLE = b'__mldr_sockpath'
SOCK_NAME = '.darlingserver.sock'

MIN_AGE_SECONDS = 60     # a younger client UI is the one starting after a match
TERM_GRACE = 4.0         # seconds between SIGTERM and SIGKILL
KILL_GRACE = 2.0
POLL_SECONDS = 5.0
COOLDOWN_SECONDS = 30.0  # minimum between two closes in the same match
MAX_CLOSES = 3           # give up on a client that keeps reopening during a match
IDLE_SECONDS = 600.0     # leave after this long without a client or a game, once one was seen


def target_prefix():
    """The prefix this launcher owns, or None when it cannot be determined."""
    value = os.environ.get('DPREFIX', str(Path.home() / '.darling'))
    prefix = Path(value).expanduser()
    return prefix if prefix.is_absolute() else None


def is_client_ui(executable):
    """True for the League client UI and its helpers, by executable name.

    The whole command line is not usable: `LeagueCrashHandler` carries the
    `LeagueClientUx` string in its arguments (attachment paths under
    `LeagueClientUx Logs`) and lives under the same `/League` paths, so only the
    file name tells the UI apart from a related process.
    """
    name = os.path.basename(executable)
    return name == UX_NAME or name.startswith(UX_NAME + ' Helper')


def match_state(proc_root, prefix):
    """Return {'game': bool, 'ui': {pid: start_ticks}}, or None if not reliable.

    None means the caller must not act. The game and the client UI are both
    attributed to `prefix` through the loader's socket path: a process of
    another install is not a match and its client is not touched.
    """
    state = {'game': False, 'ui': {}}
    owner = (prefix / SOCK_NAME).parent.resolve()
    try:
        uptime = float((proc_root / 'uptime').read_text().split()[0])
        ticks = os.sysconf('SC_CLK_TCK')
        if not math.isfinite(uptime) or uptime < 0 or ticks <= 0:
            return None
        entries = list(proc_root.iterdir())
    except (OSError, ValueError, IndexError):
        return None
    for proc in entries:
        if not proc.name.isdigit():
            continue
        try:
            if proc.stat().st_uid != os.getuid():
                continue
            raw = (proc / 'cmdline').read_bytes()
            executable = raw.split(b'\0')[0].decode(errors='replace')
            client_ui = is_client_ui(executable)
            if not client_ui and not executable.endswith(GAME_SUFFIX):
                continue
            environment = dict(item.split(b'=', 1) for item in
                               (proc / 'environ').read_bytes().split(b'\0') if b'=' in item)
            socket = environment.get(SOCK_VARIABLE)
            if socket is None or Path(os.fsdecode(socket)).parent.resolve() != owner:
                continue
            if not client_ui:
                state['game'] = True
                continue
            # Field 22 follows a comm field that may contain spaces/parentheses.
            start_ticks = int((proc / 'stat').read_text().rsplit(')', 1)[1].split()[19])
            if start_ticks < 0 or uptime - start_ticks / ticks < MIN_AGE_SECONDS:
                continue
            state['ui'][proc.name] = start_ticks
        except FileNotFoundError:
            if proc.exists():
                return None
            continue
        except (OSError, ValueError, IndexError):
            return None
    return state


def ui_identity(proc_root, pid):
    """Start ticks of a live client UI process, or None when it is not one."""
    try:
        raw = (proc_root / pid / 'cmdline').read_bytes()
        executable = raw.split(b'\0')[0].decode(errors='replace')
        if not is_client_ui(executable):
            return None
        return int((proc_root / pid / 'stat').read_text().rsplit(')', 1)[1].split()[19])
    except (OSError, ValueError, IndexError):
        return None


def close_ui(proc_root, processes):
    """Terminate the given client UI processes; return the pids signalled.

    Each pid is rechecked, including its start time, before every signal: a
    process that exited, a reused pid and a process that is no longer a client
    UI are all left alone.
    """
    closed = set()
    for sig, grace in ((signal.SIGTERM, TERM_GRACE), (signal.SIGKILL, KILL_GRACE)):
        for pid, start_ticks in processes.items():
            if ui_identity(proc_root, pid) != start_ticks:
                continue
            try:
                os.kill(int(pid), sig)
                closed.add(pid)
            except OSError:
                continue
        time.sleep(grace)
    return closed


def close_decision(state, closes, last_close, now):
    """Return 'close', 'limit' or None for one observation."""
    if not state['game'] or not state['ui']:
        return None
    if closes >= MAX_CLOSES:
        return 'limit'
    # last_close is None until the first close: monotonic time counts from boot,
    # so a 0.0 start would block closes for the first COOLDOWN_SECONDS of uptime.
    if last_close is not None and now - last_close < COOLDOWN_SECONDS:
        return None
    return 'close'


def update_idle(state, seen, idle_since, now):
    """Return (seen, idle_since) after one observation.

    The idle clock only starts once a client UI or a match has been seen: a long
    patch or login can keep the League client closed for more than IDLE_SECONDS
    after launch, and leaving then would skip the match that follows.
    """
    active = state['game'] or bool(state['ui'])
    seen = seen or active
    if active or not seen:
        return seen, None
    return seen, now if idle_since is None else idle_since


def supervise(proc_root=Path('/proc'), prefix=None, once=False, dry_run=False, out=sys.stdout):
    """Watch for a match and close the client UI while it runs."""
    prefix = prefix or target_prefix()
    if prefix is None:
        print('close-league-client-in-game: unusable DPREFIX: no action', file=out, flush=True)
        return 0
    print('close-league-client-in-game: watching %s for a running match; '
          'opt out with RCLOL_CLOSE_CLIENT_IN_GAME=0' % prefix, file=out, flush=True)
    in_game = False
    closes = 0
    last_close = None
    limit_reported = False
    seen = False
    idle_since = None
    while True:
        state = match_state(proc_root, prefix)
        now = time.monotonic()
        if state is None:
            print('close-league-client-in-game: unreadable /proc state: no action',
                  file=out, flush=True)
        else:
            if state['game'] and not in_game:
                in_game = True
                closes = 0
                last_close = None
                limit_reported = False
                print('close-league-client-in-game: a match started', file=out, flush=True)
            elif not state['game'] and in_game:
                in_game = False
                print('close-league-client-in-game: the match ended; the client can reopen',
                      file=out, flush=True)
            decision = close_decision(state, closes, last_close, now)
            if decision == 'limit':
                if not limit_reported:
                    print('close-league-client-in-game: the client reopened %d times in one '
                          'match: no further action' % closes, file=out, flush=True)
                    limit_reported = True
            elif decision == 'close':
                pids = sorted(state['ui'], key=int)
                if dry_run:
                    print('close-league-client-in-game: would close the client UI (%d '
                          'processes: %s)' % (len(pids), ','.join(pids)), file=out, flush=True)
                else:
                    closed = close_ui(proc_root, state['ui'])
                    if closed:
                        closes += 1
                        last_close = time.monotonic()
                        print('close-league-client-in-game: closed the client UI for the '
                              'running match (%d processes: %s)'
                              % (len(closed), ','.join(sorted(closed, key=int))),
                              file=out, flush=True)
            seen, idle_since = update_idle(state, seen, idle_since, now)
        if once:
            if state is not None:
                print('close-league-client-in-game: one observation: match=%s client_ui=%d'
                      % ('running' if state['game'] else 'absent', len(state['ui'])),
                      file=out, flush=True)
            return 0
        if idle_since is not None and time.monotonic() - idle_since > IDLE_SECONDS:
            print('close-league-client-in-game: no client or match for %ds: leaving'
                  % IDLE_SECONDS, file=out, flush=True)
            return 0
        time.sleep(POLL_SECONDS)
    return 0


def main():
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--once', action='store_true',
                        help='one observation and exit (diagnostics)')
    parser.add_argument('--dry-run', action='store_true',
                        help='report what it would close, without signalling anything')
    parser.add_argument('--detach', action='store_true',
                        help='leave the launcher session so a terminal hang-up cannot end it')
    args = parser.parse_args()
    if args.detach:
        try:
            os.setsid()
        except OSError:
            pass
    return supervise(once=args.once, dry_run=args.dry_run)


if __name__ == '__main__':
    sys.exit(main())
