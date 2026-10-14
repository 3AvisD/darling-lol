#!/usr/bin/env python3
"""Build and run the host-only check against Darling's actual madvise translation."""
from pathlib import Path
import subprocess

project = Path(__file__).resolve().parent.parent
folder = project / "build/logs/madvise-fix"
headers = project / "build/darling-src/src/external/xnu/darling/src/libsystem_kernel/emulation/include"
alias = folder / "include/darling/emulation"
alias.parent.mkdir(parents=True, exist_ok=True)
if not alias.exists():
    alias.symlink_to(headers, target_is_directory=True)
if alias.resolve() != headers.resolve():
    raise RuntimeError("Unexpected host-test header alias")
source = project / "build/darling-src/src/external/xnu/darling/src/libsystem_kernel/emulation/src/xnu_syscall/bsd/impl/mman/madvise.c"
binary = folder / "host-madvise-check"
subprocess.run(["cc", "-std=gnu11", "-O2", "-I", str(folder / "include"),
                str(project / "tools/riot-host-madvise-check.c"), str(source), "-o", str(binary)], check=True)
print("Built", binary)
# One process per host THP policy; the code reads the policy once per process.
for mode in ("always", "never", "madvise"):
    subprocess.run([str(binary), mode], check=True)
