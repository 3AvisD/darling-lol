#!/usr/bin/env python3
"""Rebuild the patched QuartzCore compositor and Metal presentation objects."""
import shlex
import subprocess
from pathlib import Path

project = Path(__file__).resolve().parent.parent
framework = project / 'build/darling-src/build/src/external/cocotron/QuartzCore'
flags = {}
for line in (framework / 'CMakeFiles/QuartzCore.dir/flags.make').read_text().splitlines():
    if line.startswith(('C_', 'CXX_')) and ' = ' in line:
        key, value = line.split(' = ', 1)
        flags[key] = shlex.split(value)
link = shlex.split((framework / 'CMakeFiles/QuartzCore.dir/link.txt').read_text())
sources = ['CALayer.m', 'CALayerContext.m', 'CARenderer.m', 'CATransaction.m',
           'CATransactionGroup.m', 'CATransformLayer.m', 'CAMetalLayer.mm', 'CAMetalDrawable.mm']
for source in sources:
    kind = 'CXX' if source.endswith('.mm') else 'C'
    obj = project / f'build/m336-{Path(source).stem}.o'
    subprocess.run(['clang++' if kind == 'CXX' else 'clang', *flags[kind + '_DEFINES'],
                    *flags[kind + '_INCLUDES'], *flags[kind + '_FLAGS'], '-O2',
                    '-Wno-macro-redefined', '-c',
                    str(project / 'build/darling-src/src/external/cocotron/QuartzCore' / source),
                    '-o', str(obj)], check=True)
    link[link.index(f'CMakeFiles/QuartzCore.dir/{source}.o')] = str(obj)
link[link.index('-o') + 1] = str(project / 'build/m336-QuartzCore')
subprocess.run(link, cwd=framework, check=True)
print('Built isolated QuartzCore compositor and Metal presentation')
