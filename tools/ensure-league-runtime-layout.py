#!/usr/bin/env python3
"""Recover League's stale lock and validate its existing game directory."""
import os
from pathlib import Path

prefix = Path(os.environ.get("DPREFIX", str(Path.home() / ".darling")))
app = prefix / "Applications/League of Legends.app"
game = app / "Contents/LoL/Game"
alias = app / "Game"
lockfile = app / "Contents/LoL/lockfile"
# An interrupted runtime can leave this ephemeral remoting lock behind. Riot
# waits for that old client at startup. Guest PIDs are namespaced, so identify
# a live owner by its executable mapping rather than checking the host PID.
if lockfile.is_file() and not lockfile.is_symlink():
    observed = lockfile.stat()
    owner = False
    for proc in Path("/proc").iterdir():
        if not proc.name.isdigit():
            continue
        try:
            if proc.stat().st_uid != os.getuid():
                continue
            if proc.joinpath("comm").read_text().strip() != "mldr":
                continue
            if proc.joinpath("exe").readlink().name != "mldr":
                continue
            mappings = proc.joinpath("maps").read_text()
            if str(app) + "/Contents/LoL/LeagueClient.app/Contents/MacOS/LeagueClient\n" in mappings:
                owner = True
                break
        except FileNotFoundError:
            continue
        except PermissionError:
            owner = True  # Cannot establish that the owner is gone.
            break
    if not owner:
        try:
            current = lockfile.stat()
        except FileNotFoundError:
            current = None
        if current and (current.st_ino, current.st_mtime_ns) == (observed.st_ino, observed.st_mtime_ns):
            lockfile.unlink()
            print("Cleared stale League remoting lock after its process exited")
if not (game / "LeagueofLegends.app/Contents/MacOS/LeagueofLegends").is_file():
    raise SystemExit(0)
if os.path.lexists(alias):
    if alias.resolve() != game.resolve():
        if alias.is_dir() and not alias.is_symlink():
            print("Riot created a separate Game directory; the update hook registers Contents/LoL/Game directly")
        else:
            raise SystemExit("League secondary install root conflicts with its macOS game directory")
# The startup API hook registers the native directory directly. Creating a new
# bundle-root alias here makes Riot's integrity pass see an extra directory
# and can turn the first Play action into another repair.
