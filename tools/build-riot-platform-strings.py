#!/usr/bin/env python3
"""Optimize configured string helpers, preserving every other platform object.

Every src/string/generic source is recompiled at -O2 -fno-builtin (and
-fno-strict-aliasing for this old BSD C) with the object library's own defines
and includes, then relinked into build/riot-libsystem_platform.dylib. The AVX2
and REP paths for memcpy/memmove/memset/bzero/memcmp/memchr live in those same
sources and are selected from the commpage at run time, so this builder only
has to change the optimization level.
"""
from pathlib import Path
import shlex,subprocess,shutil,sys
sys.path.insert(0,str(Path(__file__).resolve().parent))
import riot_build_gate
root=Path(__file__).resolve().parent.parent
folder=root/'build/darling-src/build/src/external/libplatform'
flags={}
for line in (folder/'CMakeFiles/platform_obj.dir/flags.make').read_text().splitlines():
 if line.startswith('C_') and ' = ' in line:
  k,v=line.split(' = ',1);flags[k]=shlex.split(v)
inc=subprocess.check_output(['clang','-print-resource-dir'],text=True).strip()+'/include'
flags['C_INCLUDES']=[inc if x.startswith('/usr/lib/clang/') and x.endswith('/include') else x for x in flags['C_INCLUDES']]
link=shlex.split((folder/'CMakeFiles/platform.dir/link.txt').read_text())
recompiled=0
riot_build_gate.wait_until_free()
for original in list(link):
 if not original.startswith('CMakeFiles/platform_obj.dir/src/string/generic/') or not original.endswith('.c.o'):continue
 relative=original.split('platform_obj.dir/',1)[1].removesuffix('.o')
 obj=root/('build/riot-platform-'+Path(relative).name+'.o')
 subprocess.run(['clang',*flags['C_DEFINES'],*flags['C_INCLUDES'],*flags['C_FLAGS'],'-O2','-fno-builtin','-fno-strict-aliasing','-c',str(root/'build/darling-src/src/external/libplatform'/relative),'-o',str(obj)],check=True)
 link[link.index(original)]=str(obj)
 recompiled+=1
link[link.index('-o')+1]=str(root/'build/riot-libsystem_platform.dylib')
subprocess.run(link,cwd=folder,check=True)
print('built build/riot-libsystem_platform.dylib with',recompiled,'string objects at -O2')
p=root/'build/m810-platform-runtime';p.mkdir(exist_ok=True);shutil.copy2(root/'build/riot-libsystem_platform.dylib',p/'libsystem_platform.dylib')
