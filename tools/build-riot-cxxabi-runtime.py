#!/usr/bin/env python3
"""Build exception RTTI matching and the cross-image catch regression."""
from pathlib import Path
import shlex
import subprocess
root = Path(__file__).resolve().parent.parent
folder = root / 'build/darling-src/build/src/external/libcxxabi'
flags = {}
for line in (folder / 'CMakeFiles/cxxabi_obj.dir/flags.make').read_text().splitlines():
    if line.startswith('CXX_') and ' = ' in line:
        key, value = line.split(' = ', 1)
        flags[key] = shlex.split(value)
link = shlex.split((folder / 'CMakeFiles/cxxabi.dir/link.txt').read_text())
obj = root / 'build/riot-cxxabi-private_typeinfo.o'
subprocess.run(['clang++', *flags['CXX_DEFINES'], *flags['CXX_INCLUDES'],
                *flags['CXX_FLAGS'], '-c', str(root / 'build/darling-src/src/external/libcxxabi/src/private_typeinfo.cpp'),
                '-o', str(obj)], check=True)
link[link.index('CMakeFiles/cxxabi_obj.dir/src/private_typeinfo.cpp.o')] = str(obj)
link[link.index('-o') + 1] = str(root / 'build/riot-libc++abi.dylib')
subprocess.run(link, cwd=folder, check=True)
obj = root / 'build/riot-cxx-exception-check.o'
subprocess.run(['clang++', *flags['CXX_DEFINES'], *flags['CXX_INCLUDES'],
                *flags['CXX_FLAGS'], '-c', str(root / 'tools/riot-cxx-exception-check.cpp'),
                '-o', str(obj)], check=True)
rtti = root / 'build/riot-cxx-exception-rtti.o'
subprocess.run(['clang', '-target', 'x86_64-apple-macosx11.0', '-c',
                str(root / 'tools/riot-cxx-exception-rtti.s'), '-o', str(rtti)], check=True)
subprocess.run(['clang++', '-target', 'x86_64-apple-macosx11.0', '-fuse-ld=lld',
                '-nostdlib', '-isysroot', '@DARLING_SDK@', str(obj), str(rtti),
                '-lSystem', '-lc++', '-lc++abi', '-o',
                str(root / 'build/riot-cxx-exception-check')], check=True)
