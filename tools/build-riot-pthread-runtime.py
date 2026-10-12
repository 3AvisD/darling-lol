#!/usr/bin/env python3
"""Build an isolated optimized pthread library using configured source aliases."""
from pathlib import Path
import shlex
import shutil
import subprocess

root = Path(__file__).resolve().parent.parent
build = root / "build/darling-src/build"
component = build / "src/external/libpthread/src"
flags, custom = {}, {}
for line in (component / "CMakeFiles/system_pthread_obj.dir/flags.make").read_text().splitlines():
    if line.startswith(("C_", "ASM_")) and " = " in line:
        key, value = line.split(" = ", 1)
        flags[key] = shlex.split(value)
    elif line.startswith("# Custom flags:") and "_FLAGS = " in line:
        key, value = line.split("_FLAGS = ", 1)
        custom[key.split("system_pthread_obj.dir/", 1)[1]] = shlex.split(value)
link = shlex.split((component / "CMakeFiles/system_pthread.dir/link.txt").read_text())
objects = [arg for arg in link if arg.startswith("CMakeFiles/system_pthread_obj.dir/") and arg.endswith(".o")]
for original in objects:
    relative = original.split("system_pthread_obj.dir/", 1)[1]
    source = relative.removesuffix(".o")
    kind = "ASM" if source.endswith((".s", ".S")) else "C"
    obj = root / ("build/riot-pthread-" + source.replace("/", "-") + ".o")
    subprocess.run(["clang", *flags.get(kind + "_DEFINES", []),
                    *flags.get(kind + "_INCLUDES", []), *flags.get(kind + "_FLAGS", []),
                    *custom.get(relative, []), "-O2", "-fno-strict-aliasing", "-c",
                    str(root / "build/darling-src/src/external/libpthread/src" / source),
                    "-o", str(obj)], check=True)
    link[link.index(original)] = str(obj)
link[link.index("-o") + 1] = str(root / "build/riot-libsystem_pthread.dylib")
subprocess.run(link, cwd=component, check=True)
candidate = root / "build/riot-pthread-check-runtime"
candidate.mkdir(exist_ok=True)
shutil.copy2(root / "build/riot-libsystem_pthread.dylib", candidate / "libsystem_pthread.dylib")

probe_flags = {}
for line in (build / "src/frameworks/CoreVideo/CMakeFiles/CoreVideo.dir/flags.make").read_text().splitlines():
    if line.startswith("C_") and " = " in line:
        key, value = line.split(" = ", 1)
        probe_flags[key] = shlex.split(value)
obj = root / "build/riot-pthread-integrity-check.o"
subprocess.run(["clang", *probe_flags["C_DEFINES"], *probe_flags["C_INCLUDES"],
                *probe_flags["C_FLAGS"], "-O2", "-c", str(root / "tools/riot-pthread-integrity-check.c"),
                "-o", str(obj)], check=True)
subprocess.run(["clang", "-target", "x86_64-apple-macosx11.0", "-fuse-ld=lld", "-nostdlib",
                "-isysroot", "@DARLING_SDK@", str(obj), "-lSystem",
                "-o", str(root / "build/riot-pthread-integrity-check")], check=True)
