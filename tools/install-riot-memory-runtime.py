#!/usr/bin/env python3
"""Install the tested reclamation library atomically, without restarting clients."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import stat
import tempfile

ROOT = Path(__file__).resolve().parent.parent


def digest(path):
    with path.open("rb") as input:
        return hashlib.file_digest(input, "sha256").hexdigest()


def install(binary, destinations, backup_folder):
    with binary.open("rb") as input:
        if input.read(4) != b"\xcf\xfa\xed\xfe":
            raise ValueError("Expected the tested 64-bit Mach-O kernel library")
    version = re.match(r"(\d+)\.(\d+)", os.uname().release)
    if not version or tuple(map(int, version.groups())) < (5, 14):
        raise ValueError("The reuse translation requires Linux 5.14 or newer")
    expected = digest(binary)
    records, staged = [], []
    # Prepare every backup and replacement before changing any destination.
    try:
        for index, destination in enumerate(destinations):
            if destination.is_symlink() or not destination.is_file():
                raise ValueError("Expected a regular installed library: " + str(destination))
            metadata = destination.stat()
            old_hash = digest(destination)
            if old_hash == expected:
                records.append({"path": str(destination), "already_current": True})
                continue
            backup = backup_folder / (str(index) + "-" + old_hash + ".dylib")
            backup_folder.mkdir(parents=True, exist_ok=True)
            if backup.exists():
                if digest(backup) != old_hash:
                    raise ValueError("The existing backup has unexpected contents")
            else:
                shutil.copy2(destination, backup)
            fd, temporary = tempfile.mkstemp(prefix=".libsystem_kernel-staged-", dir=destination.parent)
            staged.append((Path(temporary), destination))
            with os.fdopen(fd, "wb") as output, binary.open("rb") as input:
                shutil.copyfileobj(input, output)
                output.flush()
                os.fchmod(output.fileno(), stat.S_IMODE(metadata.st_mode))
                os.fchown(output.fileno(), metadata.st_uid, metadata.st_gid)
                os.fsync(output.fileno())
            if digest(Path(temporary)) != expected:
                raise ValueError("The staged library does not match the build")
            records.append({"path": str(destination), "backup": str(backup)})
        for temporary, destination in staged:
            os.replace(temporary, destination)
            directory = os.open(destination.parent, os.O_DIRECTORY)
            try:
                os.fsync(directory)
            finally:
                os.close(directory)
        if any(digest(path) != expected for path in destinations):
            raise ValueError("Installed library verification failed")
    finally:
        for temporary, _ in staged:
            if temporary.exists():
                temporary.unlink()
    return {"installed": True, "sha256": expected, "libraries": records,
            "restart_performed": False,
            "activation": "Exit the game and clients, then restart Darling; an active overlay may still expose the old library."}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, default=ROOT / "build/darling-src/build/src/external/xnu/darling/src/libsystem_kernel/libsystem_kernel.dylib")
    # A previous sudo server install can make build/backups root-owned.
    parser.add_argument("--backup-dir", type=Path, default=ROOT / "build/memory-runtime-backups")
    args = parser.parse_args()
    config = json.loads((ROOT / "build/config.json").read_text())
    prefix = Path(config["prefix"])
    destinations = [prefix / config["guest_home"].lstrip("/") / "RuntimeLibraries/libsystem_kernel.dylib"]
    # Keep the per-prefix bootstrap copy consistent; leave the system SDK alone.
    destinations.append(prefix / "usr/lib/system/libsystem_kernel.dylib")
    artifact = ROOT / "build/riot-libsystem_kernel.dylib"
    if artifact.exists():
        destinations.append(artifact)
    try:
        result = install(args.binary, destinations, args.backup_dir)
    except PermissionError as error:
        parser.exit(1, "Cannot write " + str(error.filename) + "; choose a writable --backup-dir or run the installer with sudo.\n")
    print(json.dumps(result))


if __name__ == "__main__":
    main()
