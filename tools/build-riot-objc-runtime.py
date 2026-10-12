#!/usr/bin/env python3
"""Build objc with private sized deallocation, keeping the original circular link."""
from pathlib import Path
import shlex
import subprocess
from concurrent.futures import ThreadPoolExecutor

root = Path(__file__).resolve().parent.parent
folder = root / 'build/darling-src/build/src/external/objc4/runtime'
source = root / 'build/darling-src/src/external/objc4/runtime'
flags = {}
for line in (folder / 'CMakeFiles/objc_obj.dir/flags.make').read_text().splitlines():
    if line.startswith('CXX_') and ' = ' in line:
        key, value = line.split(' = ', 1)
        flags[key] = shlex.split(value)
link = shlex.split((folder / 'CMakeFiles/objc.dir/link.txt').read_text())
replacements = []
for index, arg in enumerate(link):
    if arg.startswith('CMakeFiles/objc_obj.dir/') and arg.endswith('.mm.o'):
        name = Path(arg).name.removesuffix('.o')
        obj = root / f'build/riot-objc-{name}.o'
        replacements.append((name, obj))
        link[index] = str(obj)

def compile_object(item):
    name, obj = item
    subprocess.run(['clang++', *flags['CXX_DEFINES'], *flags['CXX_INCLUDES'],
                    *flags['CXX_FLAGS'], '-O2', '-DNDEBUG=1', '-UOBJC_IS_DEBUG_BUILD', '-DOBJC_IS_DEBUG_BUILD=0', '-c', str(source / name), '-o', str(obj)], check=True)

with ThreadPoolExecutor(max_workers=6) as pool:
    list(pool.map(compile_object, replacements))
link[link.index('-o') + 1] = str(root / 'build/riot-libobjc.A.dylib')
subprocess.run(link, cwd=folder, check=True)
obj = root / 'build/riot-objc-allocator-check.o'
subprocess.run(['clang++', *flags['CXX_DEFINES'], *flags['CXX_INCLUDES'],
                *flags['CXX_FLAGS'], '-c', str(root / 'tools/riot-objc-allocator-check.cpp'),
                '-o', str(obj)], check=True)
subprocess.run(['clang++', '-target', 'x86_64-apple-macosx11.0', '-fuse-ld=lld',
                '-nostdlib', '-isysroot', '@DARLING_SDK@', str(obj),
                '-lSystem', '-lc++', '-lobjc', '-o',
                str(root / 'build/riot-objc-allocator-check')], check=True)
