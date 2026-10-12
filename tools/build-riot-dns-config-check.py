#!/usr/bin/env python3
"""Build the DNS configuration parser probe against Darling's SDK."""
from pathlib import Path
import shlex
import subprocess

project = Path(__file__).resolve().parent.parent
flags_file = project / 'build/darling-src/build/src/external/configd/CMakeFiles/system_configuration.dir/flags.make'
flags = {}
for line in flags_file.read_text().splitlines():
    if line.startswith('C_') and ' = ' in line:
        name, value = line.split(' = ', 1)
        flags[name] = shlex.split(value)
obj = project / 'build/riot-dns-config-check.o'
subprocess.run(['clang', *flags['C_DEFINES'], *flags['C_INCLUDES'], *flags['C_FLAGS'],
                '-c', str(project / 'tools/riot-dns-config-check.c'), '-o', str(obj)], check=True)
subprocess.run(['clang', '-target', 'x86_64-apple-macosx11.0', '-fuse-ld=lld', '-nostdlib',
                '-isysroot', '@DARLING_SDK@', str(obj), '-lSystem',
                '-o', str(project / 'build/riot-dns-config-check')], check=True)
