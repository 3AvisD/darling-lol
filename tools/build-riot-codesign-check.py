#!/usr/bin/env python3
"""Build a read-only code-signature probe against Darling's SDK."""
from pathlib import Path
import shlex
import subprocess

root = Path(__file__).resolve().parent.parent
flags_file = root / 'build/darling-src/build/src/external/security/trust/trustd/CMakeFiles/trustd.dir/flags.make'
flags = {}
for line in flags_file.read_text().splitlines():
    if line.startswith('C_') and ' = ' in line:
        name, value = line.split(' = ', 1)
        flags[name] = shlex.split(value)
obj = root / 'build/riot-codesign-check.o'
subprocess.run(['clang', *flags['C_DEFINES'], *flags['C_INCLUDES'], *flags['C_FLAGS'],
                '-c', str(root / 'tools/riot-codesign-check.c'), '-o', str(obj)], check=True)
subprocess.run(['clang', '-target', 'x86_64-apple-macosx11.0', '-fuse-ld=lld', '-nostdlib',
                '-isysroot', '@DARLING_SDK@', str(obj), '-lSystem',
                '-framework', 'Security', '-framework', 'CoreFoundation',
                '-o', str(root / 'build/riot-codesign-check')], check=True)
