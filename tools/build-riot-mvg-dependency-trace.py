#!/usr/bin/env python3
"""Build optional return-preserving MVG dependency diagnostics."""
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
obj = root / 'build/riot-mvg-dependency-trace.o'
subprocess.run(['clang', *flags['C_DEFINES'], *flags['C_INCLUDES'], *flags['C_FLAGS'],
                '-c', str(root / 'tools/riot-mvg-dependency-trace.c'), '-o', str(obj)], check=True)
subprocess.run(['clang', '-target', 'x86_64-apple-macosx11.0', '-fuse-ld=lld', '-nostdlib',
                '-dynamiclib', '-isysroot', '@DARLING_SDK@', str(obj), '-lSystem',
                '-framework', 'Security', '-framework', 'CoreFoundation',
                '-framework', 'IOKit', '-o', str(root / 'build/libRiotMvgDependencyTrace.dylib')], check=True)
