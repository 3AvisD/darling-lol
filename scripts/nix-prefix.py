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

ROOT = Path(__file__).resolve().parents[1]


def has_files(path):
    return path.is_dir() and any(path.iterdir())


def is_prefix(path):
    # Darling creates this mount point in every prefix it starts.
    return (path / 'Volumes/SystemRoot').is_dir()


def move(old, new):
    new.parent.mkdir(parents=True, exist_ok=True)
    if new.is_dir():
        new.rmdir()
    try:
        os.rename(old, new)
        return
    except OSError as error:
        if error.errno != errno.EXDEV:
            raise
    partial = new.with_name(new.name + '.partial')
    if partial.exists():
        shutil.rmtree(partial)
    print(f'Copying {old} to {new}; this can take several minutes.', flush=True)
    try:
        subprocess.run(['cp', '-a', '--', str(old), str(partial)], check=True)
    except BaseException:
        shutil.rmtree(partial, ignore_errors=True)
        raise
    os.rename(partial, new)
    print(f'Removing {old}.', flush=True)
    shutil.rmtree(old)


def main():
    old = Path(os.environ['DPREFIX']).resolve()
    new = Path(sys.argv[1]).expanduser()
    if not new.is_absolute():
        raise SystemExit('The prefix must be an absolute path.')
    new = new.resolve()
    # Darling passes the prefix to overlayfs, which splits its options on these.
    if any(character in str(new) for character in ',:\n'):
        raise SystemExit('The prefix path must not contain commas, colons or newlines.')
    if new != old:
        if new.is_relative_to(old) or old.is_relative_to(new):
            raise SystemExit(f'{new} and {old} must not contain each other.')
        if has_files(new):
            if not is_prefix(new):
                raise SystemExit(f'{new} is not empty and is not a Darling prefix; '
                                 'choose a new or empty directory.')
            print(f'Using the existing prefix in {new}; {old} is left as it is.')
        elif has_files(old):
            move(old, new)
            # Darling's overlay work directory; it creates a new one next to the prefix.
            shutil.rmtree(old.with_name(old.name + '.workdir'), ignore_errors=True)
    subprocess.run([sys.executable, str(ROOT / 'scripts/configure.py'), '--prefix', str(new)],
                   cwd=ROOT, stdout=subprocess.DEVNULL, check=True)
    print(f'The prefix is now {new}. The next darling-lol launch sets up the runtime there if needed.')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
