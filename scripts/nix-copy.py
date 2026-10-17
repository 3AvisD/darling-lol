#!/usr/bin/env python3
"""Refresh managed package files without following mutable destination symlinks."""
import os
from pathlib import Path
import shutil
import stat
import sys
import tempfile

source, destination = map(Path, sys.argv[1:])
items = 'docs patches research scripts tests tools upstream LICENSE NOTICE.md README.md requirements.txt'.split()
# Preflight the entire managed tree before writing anything. Never chmod or copy
# through a link planted by a previous installation (including directory links).
for root in [destination, *destination.parents]:
    if root.is_symlink():
        raise SystemExit(f'Refusing symlink workspace ancestor: {root}')
for item in items:
    for base in [source / item, destination / item]:
        for path in [base, *base.rglob('*')] if base.is_dir() else [base]:
            if path.is_symlink():
                raise SystemExit(f'Refusing symlink in managed package tree: {path}')
            if path.exists() and not (path.is_file() or path.is_dir()):
                raise SystemExit(f'Refusing special file in managed package tree: {path}')
for item in items:
    src = source / item
    if not src.exists():
        continue
    for path in [src, *src.rglob('*')] if src.is_dir() else [src]:
        target = destination / path.relative_to(source)
        if path.is_dir():
            target.mkdir(parents=True, exist_ok=True)
            target.chmod(0o755)
        else:
            target.parent.mkdir(parents=True, exist_ok=True)
            fd, temporary = tempfile.mkstemp(prefix='.package-', dir=target.parent)
            try:
                with os.fdopen(fd, 'wb') as output, path.open('rb') as input_file:
                    shutil.copyfileobj(input_file, output)
                # Keep executable bits, remove special bits, and keep owner writable.
                os.chmod(temporary, stat.S_IMODE(path.stat().st_mode) & 0o777 | 0o600)
                os.replace(temporary, target)
            finally:
                if os.path.exists(temporary):
                    os.unlink(temporary)
