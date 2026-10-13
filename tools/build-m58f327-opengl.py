#!/usr/bin/env python3
"""Build isolated CGL candidates using the configured build's compile/link flags."""
import shlex
import subprocess
from pathlib import Path
project = Path(__file__).resolve().parent.parent
framework = project / 'build/darling-src/build/src/frameworks/OpenGL'
flags = {}
for line in (framework/'CMakeFiles/OpenGL.dir/flags.make').read_text().splitlines():
    if line.startswith('C_') and ' = ' in line:
        key, value = line.split(' = ', 1)
        flags[key] = shlex.split(value)
def run(args, cwd=project):
    result = subprocess.run(args, cwd=cwd, capture_output=True, text=True)
    if result.returncode:
        raise SystemExit(result.stderr[-4000:])
base = ['clang', *flags['C_DEFINES'], *flags['C_INCLUDES'], *flags['C_FLAGS'], '-O2', '-Wno-macro-redefined']
object_file = project/'build/m327-opengl.o'
run([*base, '-c', str(project/'build/darling-src/src/frameworks/OpenGL/OpenGL.c'), '-o', str(object_file)])
link = shlex.split((framework/'CMakeFiles/OpenGL.dir/link.txt').read_text())
link[link.index('CMakeFiles/OpenGL.dir/OpenGL.c.o')] = str(object_file)
link[link.index('-o')+1] = str(project/'build/m327-OpenGL')
run(link, framework)
for name in ['context', 'render', 'planes', 'direct', 'resolver']:
    obj = project/f'build/m327-{name}-check.o'
    source = project/'tools/riot-gl-resolver-check.c' if name == 'resolver' else project/'tools/m58f352-cgl-direct-check.c' if name == 'direct' else project/'tools/m58f340-cgl-planes-check.c' if name == 'planes' else project/f'tools/m58f327-cgl-{name}-check.c'
    run([*base, '-c', str(source), '-o', str(obj)])
    run(['clang', '-target', 'x86_64-apple-macosx11.0', '-fuse-ld=lld', '-nostdlib',
         '-isysroot', '@DARLING_SDK@', str(obj), '-lSystem', '-framework', 'OpenGL',
         '-framework', 'CoreFoundation', '-framework', 'CoreGraphics', '-framework', 'IOSurface',
         *(['-framework','QuartzCore'] if name == 'planes' else []),
         '-o', str(project/f'build/m327-cgl-{name}-check')])
print('Built isolated OpenGL candidate and CGL validation probes')
