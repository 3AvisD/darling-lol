#!/usr/bin/env python3
"""Build the OpenDirectory local-node regression check."""
from pathlib import Path
import shlex
import subprocess

root = Path(__file__).resolve().parent.parent
appkit = root / "build/darling-src/build/src/external/cocotron/AppKit"
flags = {}
for line in (appkit / "CMakeFiles/AppKit.dir/flags.make").read_text().splitlines():
    if line.startswith("C_") and " = " in line:
        key, value = line.split(" = ", 1)
        flags[key] = shlex.split(value)

name = "riot-opendirectory-session-check"
obj = root / f"build/{name}.o"
subprocess.run(["clang", *flags["C_DEFINES"], *flags["C_INCLUDES"], *flags["C_FLAGS"],
                "-fblocks", "-c", str(root / f"tools/{name}.m"), "-o", str(obj)], check=True)
subprocess.run(["clang", "-target", "x86_64-apple-macosx11.0", "-fuse-ld=lld", "-nostdlib",
                "-isysroot", "@DARLING_SDK@", str(obj), "-lSystem",
                "-framework", "Foundation", "-framework", "OpenDirectory",
                "-framework", "CoreFoundation", "-lobjc", "-o", str(root / f"build/{name}")],
               check=True)
