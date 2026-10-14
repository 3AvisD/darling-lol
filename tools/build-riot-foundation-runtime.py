#!/usr/bin/env python3
"""Build an optimized Foundation runtime, retaining the configured build.

The framework the guest loads is /System/Library/Frameworks/Foundation.framework
(the SDK's, or this prefix's copy once installed), and the configured tree has
no Foundation overlay in RuntimeFrameworks yet. Recompile every configured
Foundation object at -O2 (the same treatment CoreFoundation/AppKit already
have) and relink with the configured link line, without -DNDEBUG: the
assertion macros in this code carry side effects in places.
"""
from pathlib import Path
from concurrent.futures import ThreadPoolExecutor
import re
import shlex
import subprocess

root = Path(__file__).resolve().parent.parent
folder = root / 'build/darling-src/build/src/external/foundation'
source = root / 'build/darling-src/src/external/foundation'

flags = {}
custom = {}
for line in (folder / 'CMakeFiles/Foundation_obj.dir/flags.make').read_text().splitlines():
    if line.startswith(('C_', 'ASM_')) and ' = ' in line:
        key, value = line.split(' = ', 1)
        flags[key] = shlex.split(value)
    elif line.startswith('# Custom flags: ') and '_FLAGS = ' in line:
        key, value = line.removeprefix('# Custom flags: ').split('_FLAGS = ', 1)
        custom[Path(key).name.removesuffix('.o')] = shlex.split(value)

# The saved CMake flags name the clang resource directory of the compiler that
# configured the tree. The system compiler has since moved (22 -> 23), which
# makes <stdbool.h> disappear: follow the installed clang instead.
resource_dir = subprocess.check_output(['clang', '-print-resource-dir'], text=True).strip()


def current_clang(values):
    return [re.sub(r'/usr/lib/clang/\d+/include', resource_dir + '/include', value) for value in values]


flags = {key: current_clang(values) for key, values in flags.items()}
custom = {key: current_clang(values) for key, values in custom.items()}

# The configured link line compiles Foundation without ARC and without
# -DNDEBUG; keep every define/include/flag exactly as in flags.make and only
# append the optimization flags.
extras = ['-O2', '-fno-strict-aliasing']

link = shlex.split((folder / 'CMakeFiles/Foundation_x86_64.dir/link.txt').read_text())
objects = []
for index, arg in enumerate(link):
    if arg.startswith('CMakeFiles/Foundation_obj.dir/') and arg.endswith('.o'):
        relative = arg.split('CMakeFiles/Foundation_obj.dir/', 1)[1][:-2]
        name = Path(relative).name
        obj = root / ('build/riot-foundation-' + name + '.o')
        objects.append((relative, obj, custom.get(name, [])))
        link[index] = str(obj)
assert objects, 'Configured Foundation objects missing'


def compile_object(item):
    relative, obj, extra_flags = item
    kind = 'ASM' if relative.endswith('.S') else 'C'
    subprocess.run(['clang', *flags[kind + '_DEFINES'], *flags[kind + '_INCLUDES'],
                    *flags[kind + '_FLAGS'], *extras, *extra_flags, '-c',
                    str(source / relative), '-o', str(obj)], check=True)


with ThreadPoolExecutor(max_workers=6) as pool:
    list(pool.map(compile_object, objects))

link[link.index('-o') + 1] = str(root / 'build/riot-Foundation')
subprocess.run(link, cwd=folder, check=True)
print('Built', root / 'build/riot-Foundation', 'from', len(objects), 'objects')
