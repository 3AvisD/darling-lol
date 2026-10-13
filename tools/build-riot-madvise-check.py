#!/usr/bin/env python3
"""Build a standalone guest reclamation check; do not run or install it."""
from pathlib import Path
import json
import shlex
import subprocess

project = Path(__file__).resolve().parent.parent
folder = project / "build/logs/madvise-fix"
folder.mkdir(parents=True, exist_ok=True)
flags_path = project / "build/darling-src/build/src/frameworks/CoreServices/src/LaunchServices/CMakeFiles/LaunchServices.dir/flags.make"
flags = {}
for line in flags_path.read_text().splitlines():
    if line.startswith("C_") and " = " in line:
        name, value = line.split(" = ", 1)
        flags[name] = shlex.split(value)
object_file = folder / "riot-madvise-check.o"
subprocess.run(["clang", *flags["C_DEFINES"], *flags["C_INCLUDES"], *flags["C_FLAGS"],
                "-O2", "-Wno-macro-redefined", "-c", str(project / "tools/riot-madvise-check.c"),
                "-o", str(object_file)], check=True)
sdk = json.loads((project / "build/config.json").read_text())["sdk"]
subprocess.run(["clang", "-target", "x86_64-apple-macosx11.0", "-fuse-ld=lld", "-nostdlib",
                "-isysroot", sdk, str(object_file), "-lSystem", "-o", str(folder / "riot-madvise-check")], check=True)
print("Built", folder / "riot-madvise-check")
