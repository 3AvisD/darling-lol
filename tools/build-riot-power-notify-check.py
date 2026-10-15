#!/usr/bin/env python3
"""Build the system sleep/wake notification guest probe (IORegisterForSystemPower)."""
from pathlib import Path
import json
import shlex
import subprocess

root = Path(__file__).resolve().parent.parent
config = root / "build/config.json"
sdk = json.loads(config.read_text())["sdk"] if config.exists() else "/usr/local/libexec/darling"
appkit = root / "build/darling-src/build/src/external/cocotron/AppKit"
flags = {}
for line in (appkit / "CMakeFiles/AppKit.dir/flags.make").read_text().splitlines():
    if line.startswith("C_") and " = " in line:
        key, value = line.split(" = ", 1)
        flags[key] = shlex.split(value)

source = root / "tools/riot-power-notify-check.c"
obj = root / "build/riot-power-notify-check.o"
exe = root / "build/riot-power-notify-check"
subprocess.run(["clang", "-x", "objective-c", *flags["C_DEFINES"], *flags["C_INCLUDES"],
                *flags["C_FLAGS"], "-fblocks", "-c", str(source),
                "-o", str(obj)], check=True)
subprocess.run(["clang", "-target", "x86_64-apple-macosx11.0",
                "-fuse-ld=lld", "-nostdlib", "-isysroot",
                sdk, str(obj), "-lSystem",
                "-framework", "IOKit", "-framework", "CoreFoundation",
                "-o", str(exe)],
               check=True)
