#!/usr/bin/env python3
"""Test the shipped relative-mouse implementation without fetching Darling or opening X11."""
import argparse
import os
import time
from pathlib import Path
import shlex
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--xvfb', help='also exercise Xorg protocol on this private Xvfb executable')
args = parser.parse_args()
root = Path(__file__).resolve().parent.parent
name = 'AppKit/X11.backend/X11RelativeMouse.h'
patch = (root / 'patches/cocotron.patch').read_text()
section = patch.split(f'diff --git a/{name} b/{name}\n', 1)[1].split('\ndiff --git ', 1)[0]
# This header is a new file: all of its source lines are additions after the first hunk header.
body = section.split('@@\n', 1)[1]
assert all(line.startswith('+') for line in body.splitlines()), 'relative mouse header must be a complete added file'
with tempfile.TemporaryDirectory(prefix='darling-input-check-') as folder:
    folder = Path(folder)
    header = folder / 'X11RelativeMouse.h'
    header.write_text('\n'.join(line[1:] for line in body.splitlines()) + '\n')
    binary = folder / 'relative-mouse-check'
    subprocess.run([*shlex.split(os.environ.get('CC', 'cc')), '-std=c99', '-Wall', '-Wextra', '-Werror',
                    f'-DX11_RELATIVE_MOUSE_HEADER="{header}"', str(root / 'tools/test-x11-relative-mouse.c'),
                    '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)

    if args.xvfb:
        protocol = folder / 'relative-mouse-xorg-check'
        subprocess.run([*shlex.split(os.environ.get('CC', 'cc')), '-std=c99', '-Wall', '-Wextra', '-Werror',
                        f'-DX11_RELATIVE_MOUSE_HEADER="{header}"', str(root / 'tools/test-x11-relative-mouse-xorg.c'),
                        '-lX11', '-o', str(protocol)], check=True)
        # -displayfd probes low display numbers and can unlink an existing Xwayland socket.
        # Pick a high unused display and let Xvfb's normal lock handling protect it instead.
        number = next(n for n in range(1000, 1100)
                      if not Path(f'/tmp/.X{n}-lock').exists()
                      and not Path(f'/tmp/.X11-unix/X{n}').exists()
                      and not Path(f'/tmp/.X11-unix/X{n}_').exists())
        private_display = ':' + str(number)
        server = subprocess.Popen([args.xvfb, private_display, '-screen', '0', '3840x1080x24',
                                   '-nolisten', 'tcp', '-ac', '-noreset'],
                                  stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
        try:
            deadline = time.monotonic() + 15
            while not Path(f'/tmp/.X11-unix/X{number}').is_socket():
                if server.poll() is not None:
                    raise RuntimeError('private Xvfb failed: ' + server.stderr.read().decode())
                if time.monotonic() >= deadline:
                    raise RuntimeError('private Xvfb did not become ready')
                time.sleep(0.02)
            subprocess.run([str(protocol), private_display], check=True, timeout=20)
        finally:
            server.terminate()
            server.communicate(timeout=10)
