#!/usr/bin/env python3
"""Render local paths into ignored runtime tools; never alter the source snapshot."""
import argparse,json,os,shutil
from pathlib import Path
from common import ROOT,configuration,render
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--guest-user');p.add_argument('--prefix',type=Path);p.add_argument('--sdk',type=Path)
a=p.parse_args();c=configuration()
if a.guest_user:
    if not a.guest_user.replace('_','').replace('-','').isalnum():p.error('guest user must be a simple account name')
    c['guest_user']=a.guest_user;c['guest_home']='/Users/'+a.guest_user
if a.prefix:c['prefix']=str(a.prefix.expanduser().resolve())
if a.sdk:c['sdk']=str(a.sdk.expanduser().resolve())
c['project_root']=str(ROOT);c['host_home']=str(Path.home())
build=ROOT/'build';build.mkdir(exist_ok=True)
(build/'config.json').write_text(json.dumps(c,indent=2)+'\n')
folder=build/'runtime/tools';folder.mkdir(parents=True,exist_ok=True)
for source in (ROOT/'tools').iterdir():
    if not source.is_file():continue
    target=folder/source.name
    # Unlink the directory entry, including dangling symlinks. Any other error
    # must abort rather than following a leftover link or writing through it.
    target.unlink(missing_ok=True)
    target.write_text(render(source.read_text(),c))
    os.chmod(target,source.stat().st_mode & 0o777)
link=folder.parent/'build'
if not link.exists():link.symlink_to(build,target_is_directory=True)
print('Configured',len(list(folder.iterdir())),'runtime sources for',c['guest_home'])
