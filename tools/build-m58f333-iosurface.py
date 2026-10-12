#!/usr/bin/env python3
"""Build the IOSurface candidate without rebuilding unrelated targets."""
import shlex, subprocess
from pathlib import Path
project=Path(__file__).resolve().parent.parent
framework=project/'build/darling-src/build/src/frameworks/IOSurface'
flags={}
for line in (framework/'CMakeFiles/IOSurface.dir/flags.make').read_text().splitlines():
 if line.startswith('C_') and ' = ' in line:
  k,v=line.split(' = ',1);flags[k]=shlex.split(v)
link=shlex.split((framework/'CMakeFiles/IOSurface.dir/link.txt').read_text())
for name in ['stubs','api','constants','IOSurfaceObjC']:
 obj=project/f'build/m333-{name}.o'
 subprocess.run(['clang',*flags['C_DEFINES'],*flags['C_INCLUDES'],*flags['C_FLAGS'],'-O2','-c',str(project/f'build/darling-src/src/frameworks/IOSurface/src/{name}.m'),'-o',str(obj)],check=True)
 link[link.index(f'CMakeFiles/IOSurface.dir/src/{name}.m.o')]=str(obj)
link[link.index('-o')+1]=str(project/'build/m333-IOSurface')
subprocess.run(link,cwd=framework,check=True)
probe=project/'build/m333-lifetime.o'
subprocess.run(['clang',*flags['C_DEFINES'],*flags['C_INCLUDES'],*flags['C_FLAGS'],'-Wno-macro-redefined','-c',str(project/'tools/m58f333-iosurface-lifetime-check.c'),'-o',str(probe)],check=True)
subprocess.run(['clang','-target','x86_64-apple-macosx11.0','-fuse-ld=lld','-nostdlib','-isysroot','@DARLING_SDK@',str(probe),'-lSystem','-framework','CoreFoundation','-framework','IOSurface','-o',str(project/'build/m333-lifetime-check')],check=True)
print('Built isolated IOSurface candidate and lifetime probe')

probe=project/'build/riot-import-cache-check.o'
subprocess.run(['clang',*flags['C_DEFINES'],*flags['C_INCLUDES'],*flags['C_FLAGS'],'-O2','-c',str(project/'tools/riot-iosurface-import-cache-check.c'),'-o',str(probe)],check=True)
subprocess.run(['clang','-target','x86_64-apple-macosx11.0','-fuse-ld=lld','-nostdlib','-isysroot','@DARLING_SDK@',str(probe),'-lSystem','-framework','CoreFoundation','-framework','IOSurface','-o',str(project/'build/riot-import-cache-check')],check=True)
