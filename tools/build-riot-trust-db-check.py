#!/usr/bin/env python3
"""Link the database regression against exactly the native trustd dependencies."""
from pathlib import Path
import shlex
import subprocess

root = Path(__file__).resolve().parent.parent
component = root / "build/darling-src/build/src/external/security/trust/trustd"
flags = {}
for line in (component / "CMakeFiles/trustd.dir/flags.make").read_text().splitlines():
    if line.startswith("C_") and " = " in line:
        key, value = line.split(" = ", 1)
        flags[key] = shlex.split(value)
obj = root / "build/riot-trust-db-check.o"
subprocess.run(["clang", *flags["C_DEFINES"], *flags["C_INCLUDES"], *flags["C_FLAGS"],
                "-c", str(root / "tools/riot-trust-db-check.c"), "-o", str(obj)], check=True)
link = shlex.split((component / "CMakeFiles/trustd.dir/link.txt").read_text())
link[link.index("-o") + 1] = str(root / "build/riot-trust-db-check")
for index, argument in enumerate(link):
    if argument.endswith("trustd.c.o"):
        link[index] = str(obj)
subprocess.run(link, cwd=component, check=True)
