#!/usr/bin/env python3
"""Build changed syscall objects into an isolated kernel and a guest security probe."""
import argparse
import json
from pathlib import Path
import re
import shlex
import subprocess

root = Path(__file__).resolve().parents[1]
kernel = root/'build/darling-src/build/src/external/xnu/darling/src/libsystem_kernel'
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--source-directory', type=Path, default=root/'build/darling-src/src/external/xnu/darling/src/libsystem_kernel/emulation/src/xnu_syscall/bsd/impl/misc')
parser.add_argument('--objects-rsp', type=Path, default=kernel/'CMakeFiles/system_kernel.dir/objects1.rsp')
args = parser.parse_args()
resource = subprocess.check_output(['clang','-print-resource-dir'], text=True).strip()

def flags(path):
    result = {}
    for line in path.read_text().splitlines():
        if line.startswith('C_') and ' = ' in line:
            key,value = line.split(' = ',1)
            result[key] = [re.sub(r'/usr/lib/clang/\d+/include',resource+'/include',v) for v in shlex.split(value)]
    return result


def compile(source, output, options):
    subprocess.run(['clang',*options['C_DEFINES'],*options['C_INCLUDES'],*options['C_FLAGS'],
                    '-O2','-c',str(source),'-o',str(output)],check=True)

options = flags(kernel/'emulation/CMakeFiles/emulation.dir/flags.make')
objects = shlex.split(args.objects_rsp.read_text())
for name in ['csops.c','csrctl.c','mac_syscall.c']:
    output = root/'build'/('security-'+name+'.o')
    compile(args.source_directory/name, output, options)
    matches = [i for i,value in enumerate(objects) if value.endswith('/misc/'+name+'.o')]
    if len(matches) != 1: raise RuntimeError('Expected one kernel object for '+name)
    objects[matches[0]] = str(output)
rsp = root/'build/security-kernel-objects.rsp'
rsp.write_text(shlex.join(objects))
folder = root/'build/security-kernel-runtime'
folder.mkdir(exist_ok=True)
link = shlex.split((kernel/'CMakeFiles/system_kernel.dir/link.txt').read_text())
link[link.index('@CMakeFiles/system_kernel.dir/objects1.rsp')] = '@'+str(rsp)
link[link.index('-o')+1] = str(folder/'libsystem_kernel.dylib')
subprocess.run(link,cwd=kernel,check=True)
options = flags(root/'build/darling-src/build/src/frameworks/CoreVideo/CMakeFiles/CoreVideo.dir/flags.make')
obj = root/'build/riot-security-state-check.o'
compile(root/'tools/riot-security-state-check.c',obj,options)
sdk = json.loads((root/'build/config.json').read_text())['sdk']
subprocess.run(['clang','-target','x86_64-apple-macosx11.0','-fuse-ld=lld','-nostdlib',
                '-isysroot',sdk,str(obj),'-lSystem','-o',str(root/'build/riot-security-state-check')],check=True)
print('Built isolated kernel and security state probe; no installation performed')
