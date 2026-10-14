#!/usr/bin/env python3
"""Check the Foundation-overlay installer policy in temporary directories.

The installer is imported, never executed against a real prefix: temporary SDK,
prefix and backup directories only. Properties under test: the overlay is
seeded from the SDK framework, the built binary replaces Versions/C/Foundation
with a backup of the previous one, re-running is a no-op, and a non-Mach-O
source is refused.
"""
import importlib.util
import os
from pathlib import Path
import tempfile

ROOT = Path(__file__).resolve().parent.parent


def load_installer():
    path = ROOT / "tools/install-riot-foundation-runtime.py"
    spec = importlib.util.spec_from_file_location("riot_foundation_installer", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def write_library(path, fill):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(b"\xcf\xfa\xed\xfe" + bytes([fill]) * 4096)


def check():
    installer = load_installer()
    _, defaults = installer.parse_arguments([])
    assert defaults.binary == installer.DEFAULT_ARTIFACT, "default source is not the built artifact"
    assert defaults.backup_dir == ROOT / "build/foundation-runtime-backups", "default backup directory changed"

    with tempfile.TemporaryDirectory(prefix="riot-foundation-offline-") as directory:
        folder = Path(directory)
        sdk_framework = folder / "sdk/System/Library/Frameworks/Foundation.framework"
        old_binary = sdk_framework / "Versions/C/Foundation"
        write_library(old_binary, 0x22)
        os.symlink("C", sdk_framework / "Versions/Current")
        os.symlink("Versions/Current/Foundation", sdk_framework / "Foundation")

        framework = folder / "prefix/Users/offline-check/RuntimeFrameworks/Foundation.framework"
        backups = folder / "backups"
        artifact = folder / "riot-Foundation"
        write_library(artifact, 0x11)
        artifact_hash = installer.digest(artifact)
        old_hash = installer.digest(old_binary)

        # First run: seeds the overlay from the SDK and replaces the binary.
        result = installer.install(artifact, sdk_framework, framework, backups)
        assert result["installed"], "first installation failed"
        installed = framework / "Versions/C/Foundation"
        assert installer.digest(installed) == artifact_hash, "built library not installed"
        assert (framework / "Foundation").is_symlink() and os.readlink(framework / "Foundation") == "Versions/Current/Foundation", "outer symlink not preserved"
        assert (framework / "Versions/Current").is_symlink(), "Current symlink not preserved"
        assert any(installer.digest(path) == old_hash for path in backups.iterdir()), "no backup of the SDK binary staged"
        assert not result["restart_performed"], "installer claims a restart"

        # Second run: already current, no new backup directory entries.
        before = sorted(path.name for path in backups.iterdir())
        result = installer.install(artifact, sdk_framework, framework, backups)
        assert any(record.get("already_current") for record in result["libraries"]), "re-run not detected as current"
        assert sorted(path.name for path in backups.iterdir()) == before, "re-run changed the backups"

        # A non-Mach-O source is refused before anything is touched.
        bad = folder / "not-a-library"
        bad.write_bytes(b"not a Mach-O")
        try:
            installer.install(bad, sdk_framework, framework, backups)
        except ValueError as error:
            assert "Mach-O" in str(error), "unexpected rejection reason"
        else:
            raise AssertionError("non-Mach-O source accepted")
        assert installer.digest(installed) == artifact_hash, "refused run changed the installed library"

    print("FOUNDATION_INSTALLER_OFFLINE pass=1 default_source=artifact overlay_seeded=1 symlinks_preserved=1 backup_staged=1 rerun_noop=1 bad_source_refused=1")


if __name__ == "__main__":
    check()
