#!/usr/bin/env python3
"""Validate installed overlays, refusing missing, empty or unreadable inputs."""
import json
import os
from pathlib import Path
import subprocess
import sys

from runtime_paths import runtime_paths

ROOT = Path(__file__).resolve().parents[1]
GUEST_REQUIRED = (
    'libDarlingXShm.dylib',
    *(f'RuntimeLibraries/{name}' for name in (
        'libsystem_kernel.dylib', 'libsystem_c.dylib', 'libsystem_platform.dylib',
        'libsystem_malloc.dylib', 'libsystem_pthread.dylib',
        'libobjc.A.dylib', 'libc++abi.dylib', 'libc++.1.dylib',
        'libGL.dylib', 'libindium.dylib', 'libiridium.dylib')),
    *(f'RuntimeFrameworks/{name}.framework/Versions/{version}/{name}'
      for name, version in (
          ('AppKit', 'C'), ('QuartzCore', 'A'), ('CoreFoundation', 'A'),
          ('SystemConfiguration', 'A'), ('CFNetwork', 'A'), ('IOSurface', 'A'),
          ('OpenGL', 'A'), ('CoreVideo', 'A'), ('CoreAudio', 'A'),
          ('AudioToolbox', 'A'), ('CoreGraphics', 'A'), ('CoreText', 'A'),
          ('Metal', 'A'), ('CarbonCore', 'A'), ('LaunchServices', 'A'))),
    'RuntimeFrameworks/AppKit.framework/Versions/C/Resources/Backends/X11.backend/Contents/MacOS/X11',
    'RuntimeFrameworks/OpenGL.framework/Versions/A/Libraries/libGL.dylib',
    'Library/Audio/Plug-Ins/Components/DarlingRiotAudio.component/Contents/MacOS/CoreAudio',
)
PREFIX_REQUIRED = (
    'usr/lib/system/libsystem_kernel.dylib',
    'System/Library/Frameworks/Accelerate.framework/Versions/A/Frameworks/'
    'vecLib.framework/Versions/A/libvDSP.dylib',
    'System/Library/Frameworks/CoreServices.framework/Versions/A/Frameworks/'
    'LaunchServices.framework/Versions/A/LaunchServices',
    'System/Library/Frameworks/CoreServices.framework/Versions/A/Frameworks/'
    'CarbonCore.framework/Versions/A/CarbonCore',
)
MACHO = (b'\xcf\xfa\xed\xfe', b'\xca\xfe\xba\xbe')


def inputs(prefix, guest_home):
    def enumeration_error(error):
        raise error

    def is_macho(path):
        with path.open('rb') as source:
            return source.read(4) in MACHO

    required = ([guest_home / relative for relative in GUEST_REQUIRED] +
                [prefix / relative for relative in PREFIX_REQUIRED])
    for path in required:
        if not is_macho(path):
            raise ValueError('Expected installed Mach-O runtime binary: ' + str(path))
    binaries = set(required)
    for name in ('RuntimeLibraries', 'RuntimeFrameworks'):
        folder = guest_home / name
        if not folder.is_dir():
            raise ValueError('Missing runtime directory: ' + str(folder))
        found = []
        for directory, _, files in os.walk(folder, onerror=enumeration_error):
            for name in files:
                if any(marker in name for marker in ('.before-', '.rt-', '.llvm')):
                    continue
                path = Path(directory) / name
                if is_macho(path):
                    found.append(path)
        if not found:
            raise ValueError('No Mach-O runtime binaries found in ' + str(folder))
        binaries.update(found)
    return sorted(binaries)


def main():
    try:
        prefix, guest_home = runtime_paths(json.loads((ROOT / 'build/config.json').read_text()))
        binaries = inputs(prefix, guest_home)
        subprocess.run([sys.executable, str(ROOT / 'tools/check-darwin-tlv-calls.py'),
                        *map(str, binaries)], check=True)
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        print('Runtime validation failed: ' + str(error), file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
