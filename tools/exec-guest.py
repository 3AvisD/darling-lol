#!/usr/bin/env python3
"""Run a command without the host file descriptors this launcher inherited (anything above stderr)."""
import os
import sys


def close_inherited_fds():
    """A terminal, IDE or service manager can leak descriptors; the guest must not inherit them."""
    try:
        names = os.listdir('/proc/self/fd')
    except OSError:
        os.closerange(3, 65536)
        return
    for name in names:
        if name.isdigit() and int(name) > 2:
            try:
                os.close(int(name))
            except OSError:
                pass  # the descriptor listdir used is already closed


def main(argv):
    if len(argv) < 2:
        sys.exit('usage: exec-guest.py COMMAND [ARGUMENT]...')
    close_inherited_fds()
    os.execvp(argv[1], argv[1:])


if __name__ == '__main__':
    main(sys.argv)
