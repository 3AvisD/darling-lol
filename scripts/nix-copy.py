#!/usr/bin/env python3
"""Refresh tracked package files, never recursively deleting workspace data.

Legacy workspaces have no ownership inventory: adopt only files in the current
package, preserving unknown legacy files rather than guessing what is disposable.
The manifest is untrusted input, not authority to delete arbitrary workspace paths.
This guards pre-existing links; concurrent hostile workspace mutation is unsupported.
"""
import errno
import json
import os
from pathlib import Path, PurePosixPath
import shutil
import stat
import sys
import tempfile

ITEMS = 'docs patches research scripts tests tools upstream LICENSE NOTICE.md README.md requirements.txt'.split()
DIRECTORY_ITEMS = set(ITEMS[:7])
MANIFEST = '.nix-managed-files.json'


def refuse(message):
    raise ValueError(message)


def check_ancestors(path):
    for ancestor in [path, *path.parents]:
        if ancestor.is_symlink():
            refuse(f'Refusing symlink workspace ancestor: {ancestor}')


def inventory(root):
    files, directories = set(), set()

    def visit(path):
        mode = path.lstat().st_mode
        relative = path.relative_to(root).as_posix()
        if stat.S_ISREG(mode):
            files.add(relative)
        elif stat.S_ISDIR(mode):
            directories.add(relative)
            for child in sorted(path.iterdir()):
                visit(child)
        else:
            refuse(f'Refusing symlink or special file in managed package tree: {path}')

    for item in ITEMS:
        path = root / item
        if path.exists() or path.is_symlink():
            if path.is_dir() != (item in DIRECTORY_ITEMS) and not path.is_symlink():
                refuse(f'Invalid managed root type: {path}')
            visit(path)
    return files, directories


def load_manifest(path):
    if path.is_symlink():
        refuse(f'Refusing symlink manifest: {path}')
    if not path.exists():
        return set(), set()
    if not path.is_file():
        refuse(f'Refusing special manifest: {path}')
    data = json.loads(path.read_text())
    if (not isinstance(data, dict) or set(data) != {'version', 'files', 'directories'}
            or type(data['version']) is not int or data['version'] != 1):
        refuse('Invalid managed-file manifest schema')
    result = []
    for field in ('files', 'directories'):
        entries = data[field]
        if not isinstance(entries, list):
            refuse(f'Invalid manifest {field}')
        validated = set()
        for name in entries:
            if not isinstance(name, str):
                refuse('Non-string manifest path')
            parts = PurePosixPath(name).parts
            if not parts:
                refuse('Empty manifest path')
                continue
            if (name != '/'.join(parts) or '\\' in name or '\x00' in name
                    or any(part in ('.', '..', '/') for part in parts)
                    or parts[0] not in ITEMS
                    or (len(parts) > 1 and parts[0] not in DIRECTORY_ITEMS)
                    or (field == 'directories' and parts[0] not in DIRECTORY_ITEMS)):
                refuse(f'Unsafe manifest path: {name!r}')
            if name in validated:
                refuse(f'Duplicate manifest path: {name}')
            validated.add(name)
        result.append(validated)
    files, directories = result
    if files & directories:
        refuse('Conflicting manifest file/directory paths')
    for name in files | directories:
        if any(parent.as_posix() in files for parent in PurePosixPath(name).parents):
            refuse(f'Manifest file used as directory: {name}')
    return files, directories


def atomic_write(target, writer, mode):
    fd, temporary = tempfile.mkstemp(prefix='.package-', dir=target.parent)
    try:
        with os.fdopen(fd, 'wb') as output:
            writer(output)
        os.chmod(temporary, mode)
        os.replace(temporary, target)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)


def refresh(source, destination):
    for root in (source, destination):
        if '..' in root.parts:
            refuse(f'Refusing path traversal: {root}')
        check_ancestors(root)
    source, destination = source.absolute(), destination.absolute()
    if not source.is_dir():
        refuse(f'Missing package source: {source}')
    if source == destination or source in destination.parents or destination in source.parents:
        refuse('Package source and workspace must not overlap')
    old_files, old_directories = load_manifest(destination / MANIFEST)
    files, directories = inventory(source)
    present_files, present_directories = inventory(destination)

    # Validate ownership/type conflicts before copying or pruning anything.
    if old_files & present_directories or old_directories & present_files:
        refuse('Tracked path changed type in workspace')
    stale_files = old_files - files
    stale_directories = old_directories - directories
    if directories & (present_files - stale_files):
        refuse('Unmanaged file blocks package directory')
    replaced_directories = files & present_directories
    for name in replaced_directories:
        if name not in stale_directories:
            refuse(f'Unmanaged directory blocks package file: {name}')
        prefix = name + '/'
        if (any(p.startswith(prefix) and p not in stale_files for p in present_files)
                or any(p.startswith(prefix) and p not in stale_directories for p in present_directories)):
            refuse(f'Unmanaged data blocks directory replacement: {name}')

    for name in sorted(stale_files):
        if name in present_files:
            (destination / name).unlink()
    # rmdir only: a user-added child always keeps its containing tree alive.
    for name in sorted(stale_directories, key=lambda p: (-p.count('/'), p)):
        if name in present_directories:
            try:
                (destination / name).rmdir()
            except OSError as error:
                if error.errno not in (errno.ENOTEMPTY, errno.EEXIST):
                    raise
    destination.mkdir(parents=True, exist_ok=True)
    for name in sorted(directories, key=lambda p: (p.count('/'), p)):
        target = destination / name
        target.mkdir(parents=True, exist_ok=True)
        target.chmod(0o755)
    for name in sorted(files):
        path, target = source / name, destination / name
        target.parent.mkdir(parents=True, exist_ok=True)

        def copy(output):
            with path.open('rb') as input_file:
                shutil.copyfileobj(input_file, output)

        atomic_write(target, copy, (stat.S_IMODE(path.stat().st_mode) & 0o777) | 0o600)
    state = json.dumps({'version': 1, 'files': sorted(files),
                        'directories': sorted(directories)}, indent=2) + '\n'
    atomic_write(destination / MANIFEST, lambda output: output.write(state.encode()), 0o600)


if __name__ == '__main__':
    try:
        if len(sys.argv) != 3:
            refuse('Usage: nix-copy.py SOURCE DESTINATION')
        refresh(*map(Path, sys.argv[1:]))
    except (ValueError, OSError) as error:
        raise SystemExit(f'Package refresh refused: {error}')
