#!/usr/bin/env python3
"""Rebuild changed IPC code into owned outputs, preserving configured objects."""
from pathlib import Path
import shlex,subprocess
project=Path(__file__).resolve().parent.parent
build=project/'build/darling-src/build/src/external/darlingserver'
flags={}
for line in (build/'CMakeFiles/darlingserver.dir/flags.make').read_text().splitlines():
 if line.startswith('CXX_') and ' = ' in line:
  k,v=line.split(' = ',1);flags[k]=shlex.split(v)
link=shlex.split((build/'CMakeFiles/darlingserver.dir/link.txt').read_text())
for name in ['call','message','darlingserver','process']:
 obj=project/f'build/riot-server-{name}.o'
 subprocess.run(['clang++',*flags['CXX_DEFINES'],*flags['CXX_INCLUDES'],*flags['CXX_FLAGS'],'-c',str(project/f'build/darling-src/src/external/darlingserver/src/{name}.cpp'),'-o',str(obj)],check=True)
 link[link.index(f'CMakeFiles/darlingserver.dir/src/{name}.cpp.o')]=str(obj)
link[link.index('-o')+1]=str(project/'build/riot-darlingserver')
subprocess.run(link,cwd=build,check=True)
subprocess.run(['clang++',*flags['CXX_DEFINES'],*flags['CXX_INCLUDES'],*flags['CXX_FLAGS'],
 '-D_GNU_SOURCE','-Wno-macro-redefined',str(project/'tools/riot-host-message-check.cpp'),
 str(project/'build/riot-server-message.o'),'-o',str(project/'build/riot-host-message-check')],check=True)
