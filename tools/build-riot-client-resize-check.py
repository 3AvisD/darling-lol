#!/usr/bin/env python3
"""Build an owned X11/IOSurface window that verifies pixels after resizing."""
from pathlib import Path
import shlex
import subprocess

root = Path(__file__).resolve().parent.parent
folder = root / 'build/darling-src/build/src/external/cocotron/AppKit'
flags = {}
for line in (folder / 'CMakeFiles/X11_backend.dir/flags.make').read_text().splitlines():
    if line.startswith('C_') and ' = ' in line:
        key, value = line.split(' = ', 1)
        flags[key] = shlex.split(value)
obj = root / 'build/riot-client-resize-check.o'
subprocess.run(['clang', *flags['C_DEFINES'], *flags['C_INCLUDES'], *flags['C_FLAGS'],
                '-O2', '-Wno-macro-redefined', '-c', str(root / 'tools/riot-client-resize-check.m'),
                '-o', str(obj)], check=True)
subprocess.run(['clang', '-target', 'x86_64-apple-macosx11.0', '-fuse-ld=lld', '-nostdlib',
                '-isysroot', '@DARLING_SDK@', str(obj), '-lSystem', '-lobjc',
                '-framework', 'AppKit', '-framework', 'QuartzCore', '-framework', 'OpenGL',
                '-framework', 'IOSurface', '-framework', 'CoreFoundation', '-framework', 'Foundation',
                str(root / 'build/darling-src/build/src/native/libX11.dylib'),
                '-o', str(root / 'build/riot-client-resize-check')], check=True)
