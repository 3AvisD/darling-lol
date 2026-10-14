#!/usr/bin/env python3
"""Stop background compilation while a League game is running.

The user measures FPS in real matches, and a compile job on this six-core
machine would skew the result. Only /proc/<pid>/cmdline is read: no signal is
sent, nothing is attached to, and no process is started or stopped. The match
is on the exact argv element of the game binary, so the gate's own command line
does not match.
"""
import sys
import time
from pathlib import Path

GAME_EXECUTABLE = "Contents/MacOS/LeagueofLegends"


def game_running():
    for entry in Path("/proc").iterdir():
        if not entry.name.isdigit():
            continue
        try:
            command_line = (entry / "cmdline").read_bytes()
        except OSError:
            continue
        for argument in command_line.split(b"\0"):
            if argument.endswith(b"/" + GAME_EXECUTABLE.encode()):
                return True
    return False


def wait_until_free(poll_seconds=20):
    """Block until no game process is left, reporting each wait."""
    if not game_running():
        return
    print("A League game is running; waiting for it to exit before compiling.", flush=True)
    while game_running():
        time.sleep(poll_seconds)
    print("No game process left; compiling.", flush=True)


if __name__ == "__main__":
    if game_running():
        print("game running")
        sys.exit(0)
    print("no game process")
    sys.exit(1)
