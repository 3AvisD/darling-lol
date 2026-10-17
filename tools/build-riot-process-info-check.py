#!/usr/bin/env python3
"""Build the Foundation version/memory probe against the prepared Darling SDK."""
from pathlib import Path
import json
import re
import shlex
import subprocess

root = Path(__file__).resolve().parent.parent
folder = root / 'build/darling-src/build/src/frameworks/CoreVideo'
sdk = json.loads((root / 'build/config.json').read_text())['sdk']
flags = {}
resource = subprocess.check_output(['clang', '-print-resource-dir'], text=True).strip()
for line in (folder / 'CMakeFiles/CoreVideo.dir/flags.make').read_text().splitlines():
    if line.startswith('C_') and ' = ' in line:
        key, value = line.split(' = ', 1)
        flags[key] = [re.sub(r'/usr/lib/clang/\d+/include', resource + '/include', v) for v in shlex.split(value)]
obj = root / 'build/riot-process-info-check.o'
subprocess.run(['clang', *flags['C_DEFINES'], *flags['C_INCLUDES'], *flags['C_FLAGS'],
                '-O2', '-c', str(root / 'tools/riot-process-info-check.m'), '-o', str(obj)], check=True)
subprocess.run(['clang', '-target', 'x86_64-apple-macosx11.0', '-fuse-ld=lld', '-nostdlib',
                '-isysroot', sdk, str(obj), '-lSystem', '-lobjc',
                '-framework', 'Foundation', '-framework', 'CoreFoundation',
                '-o', str(root / 'build/riot-process-info-check')], check=True)
