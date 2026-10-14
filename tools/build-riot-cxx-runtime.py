#!/usr/bin/env python3
"""Rebuild libc++ and libc++abi with -O2.

The CMake tree has no build type, so both libraries are clang's -O0 output: the
game imports 131 functions from libc++.1.dylib (out-of-line std::string,
iostream, locale, shared_ptr/atomic refcounts, mutex, chrono, new/delete) and
13 from libc++abi.dylib, and neither has an overlay today.

This is tools/build-riot-cxx-optimized.py without -DNDEBUG: the configured
defines and includes are kept exactly, only the optimization level changes.
The earlier builder also compiles libc++ at -O2, but with assertions disabled,
which changes what the library checks at run time; the batch installer uses
this one so the shipped library differs from the configured build in code
generation only. Outputs build/riot-libc++.1.dylib and build/riot-libc++abi.dylib.
"""
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
import re
import shlex
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
import riot_build_gate

root = Path(__file__).resolve().parent.parent
resource_dir = subprocess.check_output(["clang", "-print-resource-dir"], text=True).strip()


def current_clang(values):
    return [re.sub(r"/usr/lib/clang/\d+/include", resource_dir + "/include", value) for value in values]


def build(library, object_dir, link_dir, output):
    folder = root / "build/darling-src/build/src/external" / library
    source = root / "build/darling-src/src/external" / library
    flags = {}
    for line in (folder / ("CMakeFiles/%s/flags.make" % object_dir)).read_text().splitlines():
        if line.startswith("CXX_") and " = " in line:
            key, value = line.split(" = ", 1)
            flags[key] = current_clang(shlex.split(value))
    link = shlex.split((folder / ("CMakeFiles/%s/link.txt" % link_dir)).read_text())
    jobs = []
    for index, argument in enumerate(link):
        if argument.startswith("CMakeFiles/%s/" % object_dir) and argument.endswith(".o"):
            relative = argument[len("CMakeFiles/%s/" % object_dir):-2]
            obj = root / ("build/riot-opt-%s-%s.o" % (library, relative.replace("/", "_")))
            jobs.append((relative, obj))
            link[index] = str(obj)
    print(library, "recompiling", len(jobs), "objects at -O2", flush=True)

    def compile_one(job):
        relative, obj = job
        subprocess.run(["clang++", *flags["CXX_DEFINES"], *flags["CXX_INCLUDES"], *flags["CXX_FLAGS"],
                        "-O2", "-fno-strict-aliasing", "-c", str(source / relative), "-o", str(obj)],
                       check=True, capture_output=True)

    riot_build_gate.wait_until_free()
    with ThreadPoolExecutor(max_workers=6) as pool:
        list(pool.map(compile_one, jobs))
    link[link.index("-o") + 1] = str(root / "build" / output)
    subprocess.run(link, cwd=folder, check=True)
    print("built build/" + output, len(jobs), "objects at -O2", flush=True)


if __name__ == "__main__":
    build("libcxxabi", "cxxabi_obj.dir", "cxxabi.dir", "riot-libc++abi.dylib")
    build("libcxx", "cxx_obj.dir", "cxx.dir", "riot-libc++.1.dylib")
