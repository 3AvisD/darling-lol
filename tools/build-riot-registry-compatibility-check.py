#!/usr/bin/env python3
"""Build an isolated iokitd candidate and a real-object registry/identity probe."""
import argparse
from pathlib import Path
import re
import shlex
import subprocess

root = Path(__file__).resolve().parent.parent
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--source', type=Path, default=root / 'build/darling-src/src/external/iokitd',
                    help='Prepared iokitd source directory')
args = parser.parse_args()
folder = root / 'build/darling-src/build/src/external/iokitd'
flags = {}
resource = subprocess.check_output(['clang', '-print-resource-dir'], text=True).strip()
for line in (folder / 'CMakeFiles/iokitd.dir/flags.make').read_text().splitlines():
    if line.startswith(('C_', 'CXX_')) and ' = ' in line:
        key, value = line.split(' = ', 1)
        flags[key] = [re.sub(r'/usr/lib/clang/\d+/include', resource + '/include', value) for value in shlex.split(value)]
link = shlex.split((folder / 'CMakeFiles/iokitd.dir/link.txt').read_text())


def compile(source, output, kind):
    subprocess.run(['clang++' if kind == 'CXX' else 'clang', *flags[kind + '_DEFINES'],
                    '-I' + str(args.source / 'src'), *flags[kind + '_INCLUDES'], *flags[kind + '_FLAGS'],
                    '-O2', '-c', str(source), '-o', str(output)], check=True)


for argument in link[:]:
    if argument.startswith('CMakeFiles/iokitd.dir/src/') and argument.endswith('.o'):
        relative = argument.split('iokitd.dir/', 1)[1][:-2]
        source = args.source / relative
        output = root / ('build/registry-candidate-' + source.name + '.o')
        compile(source, output, 'CXX' if source.suffix in ('.mm', '.cpp') else 'C')
        link[link.index(argument)] = str(output)
link[link.index('-o') + 1] = str(root / 'build/riot-iokitd-candidate')
subprocess.run(link, cwd=folder, check=True)
probe = root / 'build/registry-compatibility-probe.o'
compile(root / 'tools/riot-registry-compatibility-check.mm', probe, 'CXX')
link[link.index(str(root / 'build/registry-candidate-main.mm.o'))] = str(probe)
user = root / 'build/registry-compatibility-client.o'
compile(folder / 'iokitmigUser.c', user, 'C')
link.append(str(user))
link[link.index('-o') + 1] = str(root / 'build/riot-registry-compatibility-check')
subprocess.run(link, cwd=folder, check=True)
print('Built isolated iokitd candidate and registry compatibility probe')
