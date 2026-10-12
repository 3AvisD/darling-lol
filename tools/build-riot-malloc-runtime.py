#!/usr/bin/env python3
"""Build an isolated optimized allocator and its native integrity check."""
from pathlib import Path
import shlex
import subprocess
import shutil

root = Path(__file__).resolve().parent.parent
build = root / "build/darling-src/build"
component = build / "src/external/libmalloc"
flags = {}
for line in (component / "CMakeFiles/system_malloc_obj.dir/flags.make").read_text().splitlines():
    if line.startswith("C_") and " = " in line:
        key, value = line.split(" = ", 1)
        flags[key] = shlex.split(value)
resource_include = subprocess.check_output(["clang", "-print-resource-dir"], text=True).strip() + "/include"
flags["C_INCLUDES"] = [resource_include if value.startswith("/usr/lib/clang/") and value.endswith("/include") else value for value in flags["C_INCLUDES"]]
flags["C_INCLUDES"].append("-I" + str(root / "build/darling-src/src/startup/mldr/elfcalls"))
link = shlex.split((component / "CMakeFiles/system_malloc.dir/link.txt").read_text())
objects = [arg for arg in link if arg.startswith("CMakeFiles/system_malloc_obj.dir/") and arg.endswith(".o")]
for original in objects:
    name = Path(original).name.removesuffix(".o")
    obj = root / ("build/riot-malloc-" + name + ".o")
    extra = ["-DOS_VARIANT_NOTRESOLVED=1", "-DOS_VARIANT_RESOLVED=1"] if name == "nanov2_malloc.c" else []
    subprocess.run(["clang", *flags["C_DEFINES"], *flags["C_INCLUDES"], *flags["C_FLAGS"],
                    *extra, "-O2", "-fno-strict-aliasing", "-c",
                    str(root / "build/darling-src/src/external/libmalloc/src" / name),
                    "-o", str(obj)], check=True)
    link[link.index(original)] = str(obj)
link[link.index("-o") + 1] = str(root / "build/riot-libsystem_malloc.dylib")
subprocess.run(link, cwd=component, check=True)
candidate = root / "build/riot-malloc-check-runtime"
candidate.mkdir(exist_ok=True)
shutil.copy2(root / "build/riot-libsystem_malloc.dylib", candidate / "libsystem_malloc.dylib")

probe_flags = {}
for line in (build / "src/frameworks/CoreVideo/CMakeFiles/CoreVideo.dir/flags.make").read_text().splitlines():
    if line.startswith("C_") and " = " in line:
        key, value = line.split(" = ", 1)
        probe_flags[key] = shlex.split(value)
probe_flags["C_INCLUDES"] = [resource_include if value.startswith("/usr/lib/clang/") and value.endswith("/include") else value for value in probe_flags["C_INCLUDES"]]
obj = root / "build/riot-malloc-integrity-check.o"
subprocess.run(["clang", *probe_flags["C_DEFINES"], *probe_flags["C_INCLUDES"],
                *probe_flags["C_FLAGS"], "-O2", "-c", str(root / "tools/riot-malloc-integrity-check.c"),
                "-o", str(obj)], check=True)
subprocess.run(["clang", "-target", "x86_64-apple-macosx11.0", "-fuse-ld=lld", "-nostdlib",
                "-isysroot", "@DARLING_SDK@", str(obj), "-lSystem",
                "-o", str(root / "build/riot-malloc-integrity-check")], check=True)
