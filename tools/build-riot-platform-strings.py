#!/usr/bin/env python3
"""Optimize configured string helpers, preserving every other platform object."""
from pathlib import Path
import shlex,subprocess,shutil
root=Path(__file__).resolve().parent.parent
folder=root/'build/darling-src/build/src/external/libplatform'
flags={}
for line in (folder/'CMakeFiles/platform_obj.dir/flags.make').read_text().splitlines():
 if line.startswith('C_') and ' = ' in line:
  k,v=line.split(' = ',1);flags[k]=shlex.split(v)
inc=subprocess.check_output(['clang','-print-resource-dir'],text=True).strip()+'/include'
flags['C_INCLUDES']=[inc if x.startswith('/usr/lib/clang/') and x.endswith('/include') else x for x in flags['C_INCLUDES']]
link=shlex.split((folder/'CMakeFiles/platform.dir/link.txt').read_text())
for original in list(link):
 if not original.startswith('CMakeFiles/platform_obj.dir/src/string/generic/') or not original.endswith('.c.o'):continue
 relative=original.split('platform_obj.dir/',1)[1].removesuffix('.o')
 obj=root/('build/riot-platform-'+Path(relative).name+'.o')
 subprocess.run(['clang',*flags['C_DEFINES'],*flags['C_INCLUDES'],*flags['C_FLAGS'],'-O2','-fno-builtin','-fno-strict-aliasing','-c',str(root/'build/darling-src/src/external/libplatform'/relative),'-o',str(obj)],check=True)
 link[link.index(original)]=str(obj)
link[link.index('-o')+1]=str(root/'build/riot-libsystem_platform.dylib')
subprocess.run(link,cwd=folder,check=True)
p=root/'build/m810-platform-runtime';p.mkdir(exist_ok=True);shutil.copy2(root/'build/riot-libsystem_platform.dylib',p/'libsystem_platform.dylib')
