#!/usr/bin/env python3
"""Check the batch library installer and its rollback in temporary directories.

The installer is imported, never executed: no prefix, backup directory or
running process is touched. What is checked is the policy and the mechanics -
the overlays go to the per-user RuntimeLibraries directory, an existing
file is backed up before it is replaced, a file the installer created is
removed again by --rollback, a replaced file is restored byte for byte, the
staged file is verified against the built artifact, and a file that is not a
Mach-O library is refused.
"""
import importlib.util
from pathlib import Path
import tempfile

ROOT = Path(__file__).resolve().parent.parent


def load_installer():
    path = ROOT / "tools/install-riot-libs-runtime.py"
    spec = importlib.util.spec_from_file_location("riot_libs_installer", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def write_library(path, fill):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(b"\xcf\xfa\xed\xfe" + bytes([fill]) * 4096)


def check():
    installer = load_installer()
    names = [name for _, name in installer.LIBRARIES]
    assert names == ["libsystem_c.dylib", "libc++.1.dylib",
                     "libc++abi.dylib", "libsystem_platform.dylib"], "unexpected overlay set"
    with tempfile.TemporaryDirectory(prefix="riot-libs-offline-") as directory:
        folder = Path(directory)
        prefix = folder / "prefix"
        guest_home = "/Users/offline-check"
        folder_ = prefix / "Users/offline-check/RuntimeLibraries"
        built = folder / "built"
        backups = folder / "backups"
        libraries = []
        for index, name in enumerate(names):
            artifact = built / name
            write_library(artifact, 0x10 + index)
            libraries.append((artifact, folder_ / name))
        # the platform library already exists, the others do not
        write_library(folder_ / "libsystem_platform.dylib", 0x99)
        previous_platform = installer.digest(folder_ / "libsystem_platform.dylib")

        result = installer.install(libraries, backups)
        assert result["installed"], "installation failed"
        for artifact, destination in libraries:
            assert installer.digest(destination) == installer.digest(artifact), "artifact not installed"
        assert any(record.get("existed") for record in result["libraries"]), "existing file not recorded"
        assert any(not record.get("existed") for record in result["libraries"]), "created file not recorded"
        assert any(installer.digest(path) == previous_platform for path in backups.iterdir()), "no backup staged"
        assert (backups / "manifest.json").is_file(), "no manifest written"

        # installing again changes nothing and reports every file current
        again = installer.install(libraries, backups)
        assert all(record.get("already_current") for record in again["libraries"]), "second run not idempotent"

        # rollback restores the replaced file and removes the created ones
        rolled = installer.rollback(backups)
        assert installer.digest(folder_ / "libsystem_platform.dylib") == previous_platform, "replaced file not restored"
        for name in names:
            if name == "libsystem_platform.dylib":
                continue
            assert not (folder_ / name).exists(), "created file not removed: " + name
        assert str(folder_ / "libsystem_c.dylib") in rolled["removed"], "removal not reported"

        # a file that is not a Mach-O library is refused
        broken = folder / "not-a-library.dylib"
        broken.write_bytes(b"not a mach-o")
        try:
            installer.install([(broken, folder_ / "broken.dylib")], backups)
            raise AssertionError("a non-Mach-O file was accepted")
        except ValueError as error:
            assert "Mach-O" in str(error), "unexpected refusal: " + str(error)

        # destinations() puts every overlay in the per-user RuntimeLibraries
        resolved = installer.destinations(str(prefix), guest_home)
        assert [str(destination) for _, destination in resolved] == [
            str(folder_ / name) for name in names], "unexpected destinations"

        # the command-line entry point parses its arguments and installs and rolls
        # back into a temporary prefix (uses the built artifacts when they exist)
        if all(artifact.is_file() for artifact, _ in resolved):
            import contextlib, io
            cli_prefix = folder / "cli-prefix"
            cli = ["--prefix", str(cli_prefix), "--guest-home", guest_home,
                   "--backup-dir", str(folder / "cli-backups")]
            (cli_prefix / "Users/offline-check/RuntimeLibraries").mkdir(parents=True)
            with contextlib.redirect_stdout(io.StringIO()):
                installer.main(cli)
            for name in names:
                assert (cli_prefix / "Users/offline-check/RuntimeLibraries" / name).is_file(), "entry point did not install " + name
            with contextlib.redirect_stdout(io.StringIO()):
                installer.main(cli + ["--rollback"])
            assert not (cli_prefix / "Users/offline-check/RuntimeLibraries/libsystem_c.dylib").exists(), "entry point did not roll back"
    print("LIBS_INSTALLER_OFFLINE pass=1 overlays=%d backup_before_replace=1 rollback_restores=1 "
          "rollback_removes=1 idempotent=1 rejects_non_macho=1" % len(names))


if __name__ == "__main__":
    check()
