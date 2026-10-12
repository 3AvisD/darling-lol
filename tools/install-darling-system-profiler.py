#!/usr/bin/env python3
"""Install the host-backed system_profiler into the Darling prefix (no sudo)."""
import os
from pathlib import Path
import shutil

root = Path(__file__).resolve().parent
prefix = Path(os.environ.get("DPREFIX", str(Path.home() / ".darling")))
target = prefix / "usr/sbin/system_profiler"
target.parent.mkdir(parents=True, exist_ok=True)
staged = target.with_name(target.name + ".new")
shutil.copyfile(root / "darling-system_profiler.sh", staged)
staged.chmod(0o755)
staged.replace(target)
print(f"installed {target}")
