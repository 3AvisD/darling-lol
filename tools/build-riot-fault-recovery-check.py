#!/usr/bin/env python3
"""Compile the guest signal recovery probe using Darling's configured headers."""
from pathlib import Path
import shlex
import subprocess

root = Path(__file__).resolve().parent.parent
flags = {}
config = root / "build/darling-src/build/src/external/security/trust/trustd/CMakeFiles/trustd.dir/flags.make"
for line in config.read_text().splitlines():
    if line.startswith("C_") and " = " in line:
        key, value = line.split(" = ", 1)
        flags[key] = shlex.split(value)
subprocess.run(["clang", *flags["C_DEFINES"], *flags["C_INCLUDES"], *flags["C_FLAGS"],
                "-fno-omit-frame-pointer", "-fuse-ld=lld", "-nostdlib",
                "-isysroot", "@DARLING_SDK@",
                str(root / "tools/riot-fault-recovery-check.c"), "-lSystem",
                "-o", str(root / "build/riot-fault-recovery-check")], check=True)
