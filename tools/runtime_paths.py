"""Shared host-side destinations for configured runtime installers."""
import os
from pathlib import Path


def validate_runtime_paths(prefix, guest_home):
    """Return an absolute prefix/guest-home pair that cannot escape the prefix."""
    prefix, guest_home = Path(prefix), Path(guest_home)
    if not prefix.is_absolute() or not guest_home.is_absolute():
        raise ValueError("Runtime paths must be absolute host paths")
    if ".." in guest_home.parts or not guest_home.is_relative_to(prefix) or guest_home == prefix:
        raise ValueError("RIOT_GUEST_HOME must be inside DPREFIX")
    if not guest_home.resolve().is_relative_to(prefix.resolve()) or guest_home.resolve() == prefix.resolve():
        raise ValueError("RIOT_GUEST_HOME must resolve inside DPREFIX")
    return prefix, guest_home


def runtime_paths(config):
    prefix = Path(os.environ.get("DPREFIX") or config["prefix"])
    guest_home = Path(os.environ.get("RIOT_GUEST_HOME") or
                      str(prefix / config["guest_home"].lstrip("/")))
    return validate_runtime_paths(prefix, guest_home)


if __name__ == '__main__':
    import sys
    try:
        runtime_paths({'prefix': sys.argv[1], 'guest_home': '/'},)
    except (ValueError, OSError) as error:
        raise SystemExit(str(error))
