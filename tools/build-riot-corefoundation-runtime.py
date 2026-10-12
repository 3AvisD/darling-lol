#!/usr/bin/env python3
"""Build the CoreFoundation runtime with optimization, retaining local fixes."""
from pathlib import Path
from concurrent.futures import ThreadPoolExecutor
import shlex
import subprocess
root = Path(__file__).resolve().parent.parent
folder = root / 'build/darling-src/build/src/external/corefoundation'
source = root / 'build/darling-src/src/external/corefoundation'
flags = {}
custom = {}
for line in (folder / 'CMakeFiles/CoreFoundation_obj.dir/flags.make').read_text().splitlines():
    if line.startswith(('C_', 'ASM_')) and ' = ' in line:
        key, value = line.split(' = ', 1)
        flags[key] = shlex.split(value)
    elif line.startswith('# Custom flags: ') and '_FLAGS = ' in line:
        key, value = line.removeprefix('# Custom flags: ').split('_FLAGS = ', 1)
        custom[Path(key).name.removesuffix('.o')] = shlex.split(value)
# The saved CMake flags name the clang resource directory of the compiler that configured the tree. The system
# compiler has since moved (22 -> 23), which makes <stdbool.h> disappear: follow the installed clang instead.
import re
resource_dir = subprocess.check_output(['clang', '-print-resource-dir'], text=True).strip()
def current_clang(values):
    return [re.sub(r'/usr/lib/clang/\d+/include', resource_dir + '/include', value) for value in values]
flags = {key: current_clang(values) for key, values in flags.items()}
custom = {key: current_clang(values) for key, values in custom.items()}
link = shlex.split((folder / 'CMakeFiles/CoreFoundation_x86_64.dir/link.txt').read_text())
objects = []
for index, arg in enumerate(link):
    if arg.startswith('CMakeFiles/CoreFoundation_obj.dir/') and arg.endswith('.o'):
        name = Path(arg).name.removesuffix('.o')
        obj = root / ('build/riot-cf-' + name + '.o')
        objects.append((name, obj))
        link[index] = str(obj)
def compile_object(item):
    name, obj = item
    kind = 'ASM' if name.endswith('.S') else 'C'
    subprocess.run(['clang', *flags[kind+'_DEFINES'], *flags[kind+'_INCLUDES'],
                    *flags[kind+'_FLAGS'], '-O2', *custom.get(name, []), '-c',
                    str(source/name), '-o', str(obj)], check=True)
with ThreadPoolExecutor(max_workers=6) as pool:
    list(pool.map(compile_object, objects))
link[link.index('-o')+1] = str(root / 'build/riot-CoreFoundation')
subprocess.run(link, cwd=folder, check=True)
