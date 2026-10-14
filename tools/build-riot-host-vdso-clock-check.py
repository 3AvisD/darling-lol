#!/usr/bin/env python3
"""Build the host-only vDSO clock check against Darling's actual clock source."""
from pathlib import Path
import subprocess

project = Path(__file__).resolve().parent.parent
folder = project / "build/logs/vdso-clock"
headers = project / "build/darling-src/src/external/xnu/darling/src/libsystem_kernel/emulation/include"
sdk = project / "build/darling-src/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk/usr/include"
alias = folder / "include/darling/emulation"
alias.parent.mkdir(parents=True, exist_ok=True)
if not alias.exists():
    alias.symlink_to(headers, target_is_directory=True)
if alias.resolve() != headers.resolve():
    raise RuntimeError("Unexpected host-test header alias")
# The real source includes <elfcalls.h>; copy just that one SDK header instead of
# adding the whole macOS SDK to the host include path.
bridge = folder / "include/elfcalls.h"
bridge.write_bytes((sdk / "elfcalls.h").read_bytes())
source = project / "build/darling-src/src/external/xnu/darling/src/libsystem_kernel/emulation/src/linux_premigration/ext/clock_gettime.c"
binary = folder / "host-vdso-clock-check"
subprocess.run(["cc", "-std=gnu11", "-O2", "-pthread", "-I", str(folder / "include"),
                str(project / "tools/riot-host-vdso-clock-check.c"), str(source),
                "-ldl", "-o", str(binary)], check=True)
print("Built", binary)
