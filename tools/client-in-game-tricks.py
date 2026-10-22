#!/usr/bin/env python3
"""Resolve the client-in-game mode from League's local preferences.

The public launcher switch is RCLOL_CLIENT_IN_GAME_TRICKS=1. It reads
`lol-user-experience` from LCULocalPreferences.yaml:

- potatoModeEnabled must be true (client optimizations enabled);
- closeLeagueClientDuringGame true  -> close the client UI while a match runs;
- closeLeagueClientDuringGame false -> hide the client windows instead;
- missing/unreadable preferences     -> no action (none).

This helper only selects a mode. It does not change preferences or files.
"""

import re
import sys
from pathlib import Path

CLOSE_KEY = 'closeLeagueClientDuringGame'
OPTIMIZATION_KEY = 'potatoModeEnabled'
TRUE_VALUES = {'true', 'yes', '1'}
FALSE_VALUES = {'false', 'no', '0'}


def read_bool(path, key):
    """Return True/False for the last value, or None when absent/unreadable."""
    try:
        lines = Path(path).read_text().splitlines()
    except (OSError, UnicodeError):
        return None
    value = None
    for line in lines:
        match = re.match(r'^\s*' + re.escape(key) + r':\s*(\S+)', line)
        if match:
            value = match.group(1).strip('"\'').lower()
    if value in TRUE_VALUES:
        return True
    if value in FALSE_VALUES:
        return False
    return None


def mode(path):
    if read_bool(path, OPTIMIZATION_KEY) is not True:
        return 'none'
    return 'close' if read_bool(path, CLOSE_KEY) is True else 'hide'


def main(argv=None):
    argv = sys.argv[1:] if argv is None else argv
    path = argv[0] if argv else ''
    print(mode(path))
    return 0


if __name__ == '__main__':
    sys.exit(main())
