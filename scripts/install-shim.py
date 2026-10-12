#!/usr/bin/env python3
import os,subprocess
from pathlib import Path
from common import ROOT,configuration
c=configuration();guest=Path(c['prefix'])/c['guest_home'].lstrip('/')
guest.mkdir(parents=True,exist_ok=True)
source=ROOT/'build/runtime/tools/m58f101-logdir-shim.c';out=guest/'zzlogdir.dylib'
subprocess.run(['clang','-target','x86_64-apple-macosx11.0','-fuse-ld=lld','-dynamiclib','-nostdlib','-nodefaultlibs','-undefined','dynamic_lookup','-O2',str(source),'-Wl,-install_name,'+c['guest_home']+'/zzlogdir.dylib','-o',str(out)],check=True)
print('Installed launch argument shim')
