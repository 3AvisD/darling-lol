#!/usr/bin/env python3
"""Build the AppKit process lookup and X11 activation regression check."""
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

source = root / "tools/riot-running-application-check.m"
obj = root / "build/riot-running-application-check.o"
exe = root / "build/riot-running-application-check"
subprocess.run(["clang", *flags["C_DEFINES"], *flags["C_INCLUDES"],
                *flags["C_FLAGS"], "-fblocks", "-c", str(source),
                "-o", str(obj)], check=True)
subprocess.run(["clang", "-target", "x86_64-apple-macosx11.0",
                "-fuse-ld=lld", "-nostdlib", "-isysroot",
                "@DARLING_SDK@", str(obj), "-lSystem",
                "-framework", "AppKit", "-framework", "Foundation",
                "-framework", "CoreGraphics", "-lobjc", "-o", str(exe)],
               check=True)
