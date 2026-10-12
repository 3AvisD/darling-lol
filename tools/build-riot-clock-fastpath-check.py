#!/usr/bin/env python3
from pathlib import Path
import shlex, subprocess
root=Path(__file__).resolve().parent.parent
flags={}
for line in (root/'build/darling-src/build/src/frameworks/CoreVideo/CMakeFiles/CoreVideo.dir/flags.make').read_text().splitlines():
    if line.startswith('C_') and ' = ' in line:
        key,value=line.split(' = ',1); flags[key]=shlex.split(value)
obj=root/'build/riot-clock-fastpath-check.o'
subprocess.run(['clang',*flags['C_DEFINES'],*flags['C_INCLUDES'],*flags['C_FLAGS'],'-I'+str(root/'build/darling-src/src/startup/mldr/elfcalls'),'-O2','-c',str(root/'tools/riot-clock-fastpath-check.c'),'-o',str(obj)],check=True)
subprocess.run(['clang','-target','x86_64-apple-macosx11.0','-fuse-ld=lld','-nostdlib','-isysroot','@DARLING_SDK@',str(obj),'-lSystem','-o',str(root/'build/riot-clock-fastpath-check')],check=True)
