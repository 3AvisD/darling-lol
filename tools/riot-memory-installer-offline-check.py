#!/usr/bin/env python3
"""Check the memory-runtime installer policy in temporary directories.

The installer is imported, never executed: no prefix, backup directory or
running process is touched. The two properties under test are that the default
source is the prepared optimized artifact and that the artifact is never a
destination of a different library (the plain CMake build).
"""
import importlib.util
from pathlib import Path
import tempfile

ROOT = Path(__file__).resolve().parent.parent


def load_installer():
    path = ROOT / "tools/install-riot-memory-runtime.py"
    spec = importlib.util.spec_from_file_location("riot_memory_installer", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def write_library(path, fill):
    path.write_bytes(b"\xcf\xfa\xed\xfe" + bytes([fill]) * 4096)


def check():
    installer = load_installer()
    _, defaults = installer.parse_arguments([])
    assert defaults.binary == installer.DEFAULT_ARTIFACT, "default source is not the prepared artifact"
    assert defaults.artifact == installer.DEFAULT_ARTIFACT, "default artifact changed"
    with tempfile.TemporaryDirectory(prefix="riot-memory-offline-") as directory:
        folder = Path(directory)
        prefix = folder / "prefix"
        runtime = prefix / "Users/offline-check/RuntimeLibraries/libsystem_kernel.dylib"
        bootstrap = prefix / "usr/lib/system/libsystem_kernel.dylib"
        runtime.parent.mkdir(parents=True)
        bootstrap.parent.mkdir(parents=True)
        config = {"prefix": str(prefix), "guest_home": "/Users/offline-check"}
        artifact = folder / "riot-libsystem_kernel.dylib"
        backups = folder / "backups"
        write_library(artifact, 0x11)
        write_library(runtime, 0x22)
        write_library(bootstrap, 0x22)
        artifact_hash = installer.digest(artifact)
        old_hash = installer.digest(runtime)

        # The default run installs the prepared artifact into the prefix and
        # leaves the artifact itself current.
        destinations, note = installer.prefix_destinations(config, artifact, artifact)
        assert note is None, "default artifacts report a conflict"
        assert artifact in destinations, "artifact missing from its own installation"
        result = installer.install(artifact, destinations, backups)
        assert result["installed"], "default installation failed"
        assert installer.digest(runtime) == artifact_hash and installer.digest(bootstrap) == artifact_hash, "artifact not installed"
        assert installer.digest(artifact) == artifact_hash, "artifact changed by itself"
        assert any(record.get("already_current") for record in result["libraries"]), "artifact not detected as current"
        assert any(installer.digest(path) == old_hash for path in backups.iterdir()), "no backup staged"

        # An explicitly different source reaches the prefix copies only: the
        # artifact must not become a destination of that library.
        other = folder / "plain-cmake.dylib"
        write_library(other, 0x33)
        destinations, note = installer.prefix_destinations(config, other, artifact)
        assert artifact not in destinations, "artifact is a destination of a different source"
        assert note and "refusing to overwrite" in note, "no warning about the protected artifact"
        result = installer.install(other, destinations, backups)
        assert result["installed"], "explicit installation failed"
        other_hash = installer.digest(other)
        assert installer.digest(runtime) == other_hash and installer.digest(bootstrap) == other_hash, "prefix copy not updated"
        assert installer.digest(artifact) == artifact_hash, "artifact overwritten by a different source"
        assert not result.get("artifact_replaced"), "installer reports the protected artifact as replaced"
        assert any(installer.digest(path) == artifact_hash for path in backups.iterdir()), "backup of the replaced prefix copies missing"

        # A missing artifact is reported, not created.
        missing = folder / "not-built-yet.dylib"
        destinations, note = installer.prefix_destinations(config, other, missing)
        assert artifact not in destinations and "does not exist" in note, "missing artifact not reported"
    print("MEMORY_INSTALLER_OFFLINE pass=1 default_source=artifact artifact_protected=1 backups_staged=1")


if __name__ == "__main__":
    check()
