#!/usr/bin/env python3
"""Build the nullable LaunchServices error-output regression probe."""
from pathlib import Path
import shlex
import subprocess

project = Path(__file__).resolve().parent.parent
framework = project / 'build/darling-src/build/src/frameworks/CoreServices/src/LaunchServices'
flags = {}
for line in (framework / 'CMakeFiles/LaunchServices.dir/flags.make').read_text().splitlines():
    if line.startswith('C_') and ' = ' in line:
        name, value = line.split(' = ', 1)
        flags[name] = shlex.split(value)
name = 'riot-launchservices-null-error-check'
obj = project / ('build/' + name + '.o')
subprocess.run(['clang', *flags['C_DEFINES'], *flags['C_INCLUDES'], *flags['C_FLAGS'],
                '-O2', '-Wno-macro-redefined', '-c', str(project / ('tools/' + name + '.c')),
                '-o', str(obj)], check=True)
subprocess.run(['clang', '-target', 'x86_64-apple-macosx11.0', '-fuse-ld=lld', '-nostdlib',
                '-isysroot', '@DARLING_SDK@', str(obj), '-lSystem', '-framework', 'CoreServices',
                '-framework', 'CoreFoundation', '-framework', 'Foundation',
                '-o', str(project / ('build/' + name))], check=True)
