#!/usr/bin/env python3
"""Install trustd's supplied no-login account in Darling's flat-file database."""
import os
from pathlib import Path
import shutil

prefix = Path(os.environ.get("DPREFIX", str(Path.home() / ".darling")))
entries = {
    "passwd": "_trustd:*:282:282:trustd:/var/empty:/usr/bin/false",
    "master.passwd": "_trustd:*:282:282::0:0:trustd:/var/empty:/usr/bin/false",
    "group": "_trustd:*:282:_trustd",
}
def ensure_entry(path, entry):
    content = path.read_text()
    rows = [line.split(":") for line in content.splitlines() if line and not line.startswith("#")]
    if any(row[0] == "_trustd" for row in rows):
        return
    if any(len(row) > 2 and row[2] == "282" for row in rows):
        raise SystemExit(f"Cannot add trustd: guest {path.name} ID 282 is already assigned")
    backup = path.with_name(path.name + ".before-m325")
    if not backup.exists():
        shutil.copy2(path, backup)
    temporary = path.with_name(path.name + ".m325-next")
    shutil.copy2(path, temporary)
    temporary.write_text(content.rstrip("\n") + "\n" + entry + "\n")
    os.replace(temporary, path)
    print(f"Added Darling trustd {path.name} entry", flush=True)


for name, entry in entries.items():
    ensure_entry(prefix / "private/etc" / name, entry)

# A prefix-local /etc directory used for DNS overrides shadows Darling's
# stock /etc -> private/etc link. Supply its missing account files too.
etc = prefix / "etc"
if etc.is_dir() and not etc.is_symlink():
    for name in entries:
        path = etc / name
        if not os.path.lexists(path):
            shutil.copy2(prefix / "private/etc" / name, path)
            print(f"Exposed Darling /etc/{name}", flush=True)
        elif not path.is_symlink():
            ensure_entry(path, entries[name])
