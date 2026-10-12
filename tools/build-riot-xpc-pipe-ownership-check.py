#!/usr/bin/env python3
"""Build the private Mach-port ownership fixture against Darling's real XPC."""
from pathlib import Path
import shlex
import subprocess

root = Path(__file__).resolve().parent.parent
build = root / "build/darling-src/build/src/external"
flags = {}
for line in (build / "libxpc/test/CMakeFiles/test_libxpc.dir/flags.make").read_text().splitlines():
    if line.startswith("C_") and " = " in line:
        key, value = line.split(" = ", 1)
        flags[key] = shlex.split(value)
obj = root / "build/riot-xpc-pipe-ownership-check.o"
subprocess.run(["clang", *flags["C_DEFINES"], *flags["C_INCLUDES"], *flags["C_FLAGS"],
                "-c", str(root / "tools/riot-xpc-pipe-ownership-check.c"), "-o", str(obj)], check=True)
component = build / "security/trust/trustd"
link = shlex.split((component / "CMakeFiles/trustd.dir/link.txt").read_text())
link[link.index("-o") + 1] = str(root / "build/riot-xpc-pipe-ownership-check")
for index, argument in enumerate(link):
    if argument.endswith("trustd.c.o"):
        link[index] = str(obj)
subprocess.run(link, cwd=component, check=True)
