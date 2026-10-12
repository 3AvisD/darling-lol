#!/usr/bin/env python3
"""Rebuild libc++abi and libc++ with -O2. The CMake tree has no build type, so the stock objects (and the earlier
single-file libc++abi rebuild) are -O0: std::type_info::operator==, __dynamic_cast, shared_ptr internals ran 3-5x slower
than they should (about 2% of the League main thread).  Outputs build/riot-opt-libc++abi.dylib / riot-opt-libc++.1.dylib.
"""
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
import re, shlex, subprocess, sys
root = Path(__file__).resolve().parent.parent
resource_dir = subprocess.check_output(['clang', '-print-resource-dir'], text=True).strip()
def current_clang(values):
    return [re.sub(r'/usr/lib/clang/\d+/include', resource_dir + '/include', v) for v in values]
def build(lib, objdir, linkdir, output):
    folder = root / 'build/darling-src/build/src/external' / lib
    src = root / 'build/darling-src/src/external' / lib
    flags = {}
    for line in (folder / ('CMakeFiles/%s/flags.make' % objdir)).read_text().splitlines():
        if line.startswith('CXX_') and ' = ' in line:
            k, v = line.split(' = ', 1); flags[k] = current_clang(shlex.split(v))
    link = shlex.split((folder / ('CMakeFiles/%s/link.txt' % linkdir)).read_text())
    jobs = []
    for i, arg in enumerate(link):
        if arg.startswith('CMakeFiles/%s/' % objdir) and arg.endswith('.o'):
            rel = arg[len('CMakeFiles/%s/' % objdir):-2]            # e.g. src/algorithm.cpp
            obj = root / ('build/riot-opt-%s-%s.o' % (lib, rel.replace('/', '_')))
            jobs.append((rel, obj)); link[i] = str(obj)
    def compile_one(job):
        rel, obj = job
        subprocess.run(['clang++', *flags['CXX_DEFINES'], *flags['CXX_INCLUDES'], *flags['CXX_FLAGS'], '-O2', '-DNDEBUG',
                        '-c', str(src / rel), '-o', str(obj)], check=True)
    with ThreadPoolExecutor(max_workers=8) as pool:
        list(pool.map(compile_one, jobs))
    link[link.index('-o') + 1] = str(root / 'build' / output)
    subprocess.run(link, cwd=folder, check=True)
    print('built', output, len(jobs), 'objects')
targets = sys.argv[1:] or ['abi', 'cxx']
if 'abi' in targets: build('libcxxabi', 'cxxabi_obj.dir', 'cxxabi.dir', 'riot-opt-libc++abi.dylib')
if 'cxx' in targets: build('libcxx', 'cxx_obj.dir', 'cxx.dir', 'riot-opt-libc++.1.dylib')
