#!/usr/bin/env python3
"""Build CoreText's font-list compatibility override and standalone regression."""
from pathlib import Path
import shlex
import subprocess

root = Path(__file__).resolve().parent.parent
folder = root / 'build/darling-src/build/src/external/cocotron/CoreText'
flags = {}
for line in (folder / 'CMakeFiles/CoreText.dir/flags.make').read_text().splitlines():
    if line.startswith('C_') and ' = ' in line:
        key, value = line.split(' = ', 1)
        flags[key] = shlex.split(value)
link = shlex.split((folder / 'CMakeFiles/CoreText.dir/link.txt').read_text())
for name in ['CTFontDescriptor', 'CTFontManager']:
    obj = root / f'build/riot-{name}.o'
    subprocess.run(['clang', *flags['C_DEFINES'], *flags['C_INCLUDES'], *flags['C_FLAGS'],
                    '-O2', '-Wno-macro-redefined', '-c',
                    str(root / f'build/darling-src/src/external/cocotron/CoreText/{name}.m'),
                    '-o', str(obj)], check=True)
    link[link.index(f'CMakeFiles/CoreText.dir/{name}.m.o')] = str(obj)
link[link.index('-o') + 1] = str(root / 'build/riot-CoreText')
fontconfig = str(root / 'build/darling-src/build/src/native/libfontconfig.dylib')
if not any(value.endswith('/libfontconfig.dylib') and not value.startswith('-') for value in link):
    link.append(fontconfig)
subprocess.run(link, cwd=folder, check=True)
obj = root / 'build/riot-coretext-font-list-check.o'
subprocess.run(['clang', *flags['C_DEFINES'], *flags['C_INCLUDES'], *flags['C_FLAGS'],
                '-O2', '-Wno-macro-redefined', '-c', str(root / 'tools/riot-coretext-font-list-check.m'),
                '-o', str(obj)], check=True)
subprocess.run(['clang', '-target', 'x86_64-apple-macosx11.0', '-fuse-ld=lld', '-nostdlib',
                '-isysroot', '@DARLING_SDK@', str(obj), str(root / 'build/riot-CoreText'),
                '-lSystem', '-lobjc', '-framework', 'Foundation', '-framework', 'CoreFoundation',
                '-o', str(root / 'build/riot-coretext-font-list-check')], check=True)
