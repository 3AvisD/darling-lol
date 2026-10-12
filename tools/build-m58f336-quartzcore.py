#!/usr/bin/env python3
"""Build only the changed GPU frame publisher and relink QuartzCore."""
import shlex,subprocess
from pathlib import Path
project=Path(__file__).resolve().parent.parent
framework=project/'build/darling-src/build/src/external/cocotron/QuartzCore'
flags={}
for line in (framework/'CMakeFiles/QuartzCore.dir/flags.make').read_text().splitlines():
 if line.startswith('C_') and ' = ' in line:
  k,v=line.split(' = ',1);flags[k]=shlex.split(v)
link=shlex.split((framework/'CMakeFiles/QuartzCore.dir/link.txt').read_text())
for name in ('CALayer','CALayerContext','CATransaction'):
 obj=project/('build/m336-'+name+'.o')
 subprocess.run(['clang',*flags['C_DEFINES'],*flags['C_INCLUDES'],*flags['C_FLAGS'],'-O2','-Wno-macro-redefined','-c',str(project/('build/darling-src/src/external/cocotron/QuartzCore/'+name+'.m')),'-o',str(obj)],check=True)
 link[link.index('CMakeFiles/QuartzCore.dir/'+name+'.m.o')]=str(obj)
link[link.index('-o')+1]=str(project/'build/m336-QuartzCore')
subprocess.run(link,cwd=framework,check=True)
print('Built isolated QuartzCore frame publisher')
