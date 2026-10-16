#!/usr/bin/env python3
"""Build changed display code and run a native probe in an isolated overlay.

Requires prepared sources, an existing CMake build, Darling, and the per-user
AppKit/CoreGraphics runtime overlays. Does not install binaries or send input.
"""
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parent.parent
config = json.loads((root / 'build/config.json').read_text())
frameworks = root / 'build/darling-src/build/src/external/cocotron'
subprocess.run(['python3', str(root / 'tools/build-riot-x11-runtime.py')], check=True)

def flags_for(framework, target):
    flags = {}
    for line in (framework / f'CMakeFiles/{target}.dir/flags.make').read_text().splitlines():
        if line.startswith('C_') and ' = ' in line:
            key, value = line.split(' = ', 1)
            flags[key] = shlex.split(value)
    return flags['C_DEFINES'] + flags['C_INCLUDES'] + flags['C_FLAGS']

cg = frameworks / 'CoreGraphics'
obj = root / 'build/pr12-CGDirectDisplay.o'
subprocess.run(['clang', *flags_for(cg, 'CoreGraphics'), '-O2', '-c',
                str(root / 'build/darling-src/src/external/cocotron/CoreGraphics/CGDirectDisplay.m'),
                '-o', str(obj)], check=True)
link = shlex.split((cg / 'CMakeFiles/CoreGraphics.dir/link.txt').read_text())
link[link.index('CMakeFiles/CoreGraphics.dir/CGDirectDisplay.m.o')] = str(obj)
link[link.index('-o') + 1] = str(root / 'build/pr12-CoreGraphics')
subprocess.run(link, cwd=cg, check=True)
probe = root / 'build/riot-display-geometry-check'
subprocess.run(['clang', *flags_for(frameworks / 'AppKit', 'X11_backend'),
                '-O2', '-UNDEBUG', '-Wno-macro-redefined', '-c', str(root / 'tools/riot-display-geometry-check.m'),
                '-o', str(probe) + '.o'], check=True)
subprocess.run(['clang', '-target', 'x86_64-apple-macosx11.0', '-fuse-ld=lld',
                '-nostdlib', '-isysroot', config['sdk'], str(probe) + '.o',
                '-lSystem', '-framework', 'AppKit', '-framework', 'Foundation',
                '-framework', 'CoreGraphics', '-lobjc',
                '-L' + str(Path(config['sdk']) / 'usr/lib/native'), '-lX11', '-o', str(probe)], check=True)

original = Path(config['prefix']) / config['guest_home'].lstrip('/') / 'RuntimeFrameworks'
with tempfile.TemporaryDirectory(prefix='pr12-display-frameworks-', dir=root / 'build') as directory:
    overlay = Path(directory)
    for name in ('AppKit', 'CoreGraphics'):
        shutil.copytree(original / f'{name}.framework', overlay / f'{name}.framework',
                        symlinks=True, dirs_exist_ok=True,
                        ignore=shutil.ignore_patterns('*.before-*'))
    shutil.copy2(root / 'build/riot-X11', overlay / 'AppKit.framework/Versions/C/Resources/Backends/X11.backend/Contents/MacOS/X11')
    shutil.copy2(root / 'build/pr12-CoreGraphics', overlay / 'CoreGraphics.framework/Versions/A/CoreGraphics')
    env = dict(os.environ, EGL_PLATFORM='x11',
               DYLD_FRAMEWORK_PATH='/Volumes/SystemRoot' + str(overlay) + ':' + config['guest_home'] + '/RuntimeFrameworks',
               DYLD_LIBRARY_PATH=config['guest_home'] + '/RuntimeLibraries')
    # File capture bounds the wait even if a guest service inherits the output fd.
    log = root / 'build/pr12-display-check.log'
    with log.open('w+') as output:
        try:
            result = subprocess.run(['darling', 'shell', '/Volumes/SystemRoot' + str(probe)],
                                    env=env, stdout=output, stderr=subprocess.STDOUT, timeout=45)
        except subprocess.TimeoutExpired:
            raise SystemExit(f'Display probe timed out; inspect {log}')
        output.seek(0)
        lines = [line.rstrip() for line in output if line.startswith('DISPLAY_GEOMETRY')]
        print('\n'.join(lines))
        if result.returncode or len(lines) != 7:
            raise SystemExit(f'Display probe failed ({result.returncode}); inspect {log}')
