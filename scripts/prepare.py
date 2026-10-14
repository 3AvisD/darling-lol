#!/usr/bin/env python3
"""Fetch pinned Darling sources and apply each patch to its owning repository."""
import argparse,json,os,subprocess,tempfile
from pathlib import Path
from common import ROOT,render
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--source',type=Path,default=ROOT/'build/darling-src')
p.add_argument('--check-only',action='store_true',help='validate against temporary base indexes, without changing existing source')
a=p.parse_args();source=a.source.resolve();manifest=json.loads((ROOT/'upstream/manifest.json').read_text())
def run(args,**kwargs):return subprocess.run(args,check=True,**kwargs)
if not (source/'.git').exists():
    if a.check_only:p.error('check-only requires an existing pinned source checkout')
    source.parent.mkdir(parents=True,exist_ok=True)
    run(['git','clone',manifest['upstream'],str(source)])
    run(['git','-C',str(source),'checkout','--detach',manifest['revision']])
    # Recursive initialization can spend a long time fetching nested repos (and
    # Git LFS assets). Explicit progress keeps the last completed submodule
    # from looking like the operation that is currently stuck. The pinned
    # darling-swift .lfsconfig points at the retired git-lfs.darlinghq.org, so
    # fetch that submodule's LFS objects from GitHub, which still serves them;
    # otherwise the checkout blocks on the dead host right after sqlite.
    print('Initializing Darling submodules (large repositories/LFS assets may take a while)...',flush=True)
    run(['git','-C',str(source),'-c','lfs.url=https://github.com/darlinghq/darling-swift.git/info/lfs',
         'submodule','update','--init','--recursive','--progress'])
    print('Darling submodules initialized.',flush=True)
# Never reset someone else's checkout to make a patch apply.
for component in manifest['components']:
    repo=source/component['path']
    actual=subprocess.check_output(['git','-C',str(repo),'rev-parse','HEAD'],text=True).strip()
    if actual!=component['revision']:raise SystemExit(f"Wrong revision for {component['path']}: expected {component['revision']}, got {actual}")
    patch=render((ROOT/component['patch']).read_text())
    if a.check_only:
        with tempfile.TemporaryDirectory(prefix='darling-patch-index-') as tmp:
            env=dict(os.environ,GIT_INDEX_FILE=str(Path(tmp)/'index'))
            run(['git','-C',str(repo),'read-tree',component['revision']],env=env)
            run(['git','-C',str(repo),'apply','--cached','--check','--whitespace=nowarn','-'],input=patch,text=True,env=env)
    else:
        already=subprocess.run(['git','-C',str(repo),'apply','--reverse','--check','-'],input=patch,text=True,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL).returncode==0
        if not already:
            run(['git','-C',str(repo),'apply','--check','-'],input=patch,text=True)
            run(['git','-C',str(repo),'apply','--whitespace=nowarn','-'],input=patch,text=True)
    print(('Checked ' if a.check_only else 'Prepared ')+component['path'],flush=True)
print('All',len(manifest['components']),'component patches validated' if a.check_only else 'component patches ready')
