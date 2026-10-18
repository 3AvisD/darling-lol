#!/usr/bin/env python3
"""darling-lol --set-prefix PATH: choose where the guest prefix, and with it the
installed games, is stored.

An existing prefix is moved to PATH. Across filesystems it is copied first and
the old copy is removed only once the new one is complete. If PATH already holds
a Darling prefix, the launcher switches to it and leaves both prefixes alone.
"""
import errno
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def has_files(path):
    return path.is_dir() and any(path.iterdir())


def is_prefix(path):
    # Darling creates this mount point in every prefix it starts.
    return (path / 'Volumes/SystemRoot').is_dir()


XATTR = 'user.darling-lol-prefix-check'


def user_xattrs(path):
    try:
        return {name: os.getxattr(path, name, follow_symlinks=False)
                for name in os.listxattr(path, follow_symlinks=False) if name.startswith('user.')}
    except OSError as error:
        if error.errno in (errno.ENOTSUP, errno.ENOENT):
            return {}
        raise


def check_xattrs(folder):
    """Darling keeps state in user.* xattrs; cp -a ignores a target that drops them."""
    probe = folder / '.xattr-check'
    probe.touch()
    try:
        os.setxattr(probe, XATTR, b'1')
        if os.getxattr(probe, XATTR) != b'1':
            raise OSError(errno.ENOTSUP, 'xattr changed')
    except OSError as error:
        raise SystemExit(f'{folder.parent} does not support user extended attributes ({error.strerror}), '
                         'which the prefix needs. Choose a drive with a Linux filesystem '
                         '(ext4, btrfs, xfs); nothing was moved.')
    finally:
        probe.unlink()


def verify_copy(old, new):
    """The copy must match the original: names, types and user.* xattrs."""
    for folder, folders, files in os.walk(old):
        target = new / Path(folder).relative_to(old)
        for name in [*folders, *files]:
            source = Path(folder) / name
            copy = target / name
            if not copy.is_symlink() and not copy.exists() or user_xattrs(source) != user_xattrs(copy):
                raise SystemExit(f'The copy of {source} differs from the original; the old prefix was kept.')
        if user_xattrs(folder) != user_xattrs(target):
            raise SystemExit(f'The copy of {folder} differs from the original; the old prefix was kept.')


def move(old, new):
    """Put the prefix at new. Returns old if it still has to be removed, else None."""
    new.parent.mkdir(parents=True, exist_ok=True)
    if new.is_dir():
        new.rmdir()
    try:
        os.rename(old, new)
        return None
    except OSError as error:
        if error.errno != errno.EXDEV:
            raise
    # A directory made for this move: never touch anything that was already there.
    staging = Path(tempfile.mkdtemp(prefix=new.name + '.partial-', dir=new.parent))
    try:
        check_xattrs(staging)
        print(f'Copying {old} to {new}; this can take several minutes.', flush=True)
        subprocess.run(['cp', '-a', '--', str(old), str(staging / 'prefix')], check=True)
        verify_copy(old, staging / 'prefix')
        os.rename(staging / 'prefix', new)
    finally:
        shutil.rmtree(staging, ignore_errors=True)
    return old


def remove(path):
    try:
        shutil.rmtree(path)
    except OSError as error:
        print(f'Could not remove {path} ({error}); delete it by hand to free the space.', file=sys.stderr)


def main():
    old = Path(os.environ['DPREFIX']).resolve()
    new = Path(sys.argv[1]).expanduser()
    if not new.is_absolute():
        raise SystemExit('The prefix must be an absolute path.')
    new = new.resolve()
    # Darling passes the prefix to overlayfs, which splits its options on these.
    if any(character in str(new) for character in ',:\n'):
        raise SystemExit('The prefix path must not contain commas, colons or newlines.')
    # Darling's overlay work directory; it creates a new one next to the prefix.
    workdir = old.with_name(old.name + '.workdir')
    leftovers = []
    if new != old:
        if new.is_relative_to(old) or old.is_relative_to(new):
            raise SystemExit(f'{new} and {old} must not contain each other.')
        if new.is_relative_to(workdir) or workdir.is_relative_to(new):
            raise SystemExit(f'{new} must not be inside {workdir}, which is removed after the move.')
        if has_files(new):
            if not is_prefix(new):
                raise SystemExit(f'{new} is not empty and is not a Darling prefix; '
                                 'choose a new or empty directory.')
            print(f'Using the existing prefix in {new}; {old} is left as it is.')
        elif has_files(old):
            print(f'Moving the prefix {old} to {new}.', flush=True)
            if move(old, new):
                leftovers.append(old)
            leftovers.append(workdir)
    # Save the new path before deleting anything, so a failed delete leaves a working install.
    subprocess.run([sys.executable, str(ROOT / 'scripts/configure.py'), '--prefix', str(new)],
                   cwd=ROOT, stdout=subprocess.DEVNULL, check=True)
    for path in leftovers:
        remove(path)
    print(f'The prefix is now {new}. The next darling-lol launch sets up the runtime there if needed.')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
