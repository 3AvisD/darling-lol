#!/usr/bin/env python3
"""Build a real guest Mach exception state/recovery fixture."""
from pathlib import Path
import shlex
import subprocess

root = Path(__file__).resolve().parent.parent
build = root / "build/darling-src/build"
flags = {}
config = build / "src/external/security/trust/trustd/CMakeFiles/trustd.dir/flags.make"
for line in config.read_text().splitlines():
    if line.startswith("C_") and " = " in line:
        key, value = line.split(" = ", 1)
        flags[key] = shlex.split(value)
generated = build / "src/external/xnu/darling/src/libsystem_kernel/emulation/src/other/signal"
subprocess.run([
    "clang", *flags["C_DEFINES"], *flags["C_INCLUDES"], *flags["C_FLAGS"],
    "-I" + str(generated), "-fno-omit-frame-pointer", "-fuse-ld=lld", "-nostdlib",
    "-isysroot", "@DARLING_SDK@",
    str(root / "tools/riot-mach-exception-state-check.c"),
    str(generated / "mach_excServer.c"), "-lSystem",
    "-o", str(root / "build/riot-mach-exception-state-check"),
], check=True)
