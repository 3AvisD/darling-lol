#!/usr/bin/env python3
"""Rebuild CarbonCore's legacy Gestalt version query and its standalone probe."""
from pathlib import Path
import shlex
import subprocess

root = Path(__file__).resolve().parent.parent
folder = root / 'build/darling-src/build/src/frameworks/CoreServices/src/CarbonCore'
flags = {}
for line in (folder / 'CMakeFiles/CarbonCore.dir/flags.make').read_text().splitlines():
    if line.startswith(('C_', 'CXX_')) and ' = ' in line:
        key, value = line.split(' = ', 1)
        flags[key] = shlex.split(value)
obj = root / 'build/riot-Gestalt.o'
subprocess.run(['clang++', *flags['CXX_DEFINES'], *flags['CXX_INCLUDES'], *flags['CXX_FLAGS'],
                '-O2', '-Wno-macro-redefined', '-c',
                str(root / 'build/darling-src/src/frameworks/CoreServices/src/CarbonCore/Gestalt.cpp'),
                '-o', str(obj)], check=True)
link = shlex.split((folder / 'CMakeFiles/CarbonCore.dir/link.txt').read_text())
link[link.index('CMakeFiles/CarbonCore.dir/Gestalt.cpp.o')] = str(obj)
link[link.index('-o') + 1] = str(root / 'build/riot-CarbonCore')
subprocess.run(link, cwd=folder, check=True)
probe = root / 'build/riot-gestalt-version-check.o'
subprocess.run(['clang++', *flags['CXX_DEFINES'], *flags['CXX_INCLUDES'], *flags['CXX_FLAGS'],
                '-O2', '-Wno-macro-redefined', '-c', str(root / 'tools/riot-gestalt-version-check.cpp'),
                '-o', str(probe)], check=True)
subprocess.run(['clang++', '-target', 'x86_64-apple-macosx11.0', '-fuse-ld=lld', '-nostdlib',
                '-isysroot', '@DARLING_SDK@', str(probe), str(root / 'build/riot-CarbonCore'),
                '-lSystem', '-lc++', '-o', str(root / 'build/riot-gestalt-version-check')], check=True)
