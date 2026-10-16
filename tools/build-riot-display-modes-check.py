#!/usr/bin/env python3
"""Build the CGDisplayCopyAllDisplayModes NULL-result probe against Darling's SDK."""
from pathlib import Path
import shlex
import subprocess

root = Path(__file__).resolve().parent.parent
folder = root / 'build/darling-src/build/src/external/cocotron/CoreGraphics'
flags = {}
for line in (folder / 'CMakeFiles/CoreGraphics.dir/flags.make').read_text().splitlines():
    if line.startswith('C_') and ' = ' in line:
        key, value = line.split(' = ', 1)
        flags[key] = shlex.split(value)
obj = root / 'build/riot-display-modes-check.o'
subprocess.run(['clang', *flags['C_DEFINES'], *flags['C_INCLUDES'], *flags['C_FLAGS'],
                '-Wno-macro-redefined', '-c', str(root / 'tools/riot-display-modes-check.c'),
                '-o', str(obj)], check=True)
subprocess.run(['clang', '-target', 'x86_64-apple-macosx11.0', '-fuse-ld=lld', '-nostdlib',
                '-isysroot', '@DARLING_SDK@', str(obj), '-lSystem',
                '-framework', 'CoreGraphics', '-framework', 'CoreFoundation',
                '-o', str(root / 'build/riot-display-modes-check')], check=True)
