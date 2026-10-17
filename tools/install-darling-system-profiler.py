#!/usr/bin/env python3
"""Install the host-backed system_profiler into the Darling prefix (no sudo)."""
import json
from pathlib import Path
import shutil

from runtime_paths import runtime_paths

root = Path(__file__).resolve().parent
prefix, _ = runtime_paths(json.loads((root.parent / "build/config.json").read_text()))
target = prefix / "usr/sbin/system_profiler"
target.parent.mkdir(parents=True, exist_ok=True)
staged = target.with_name(target.name + ".new")
shutil.copyfile(root / "darling-system_profiler.sh", staged)
staged.chmod(0o755)
staged.replace(target)
print(f"installed {target}")
