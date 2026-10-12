#!/usr/bin/env python3
"""Validate the public source package without launching apps or installing anything."""
import ast,json,re,subprocess,sys
from pathlib import Path
from common import ROOT
subprocess.run([sys.executable,str(ROOT/'scripts/configure.py')],check=True)
files=subprocess.check_output(['git','-C',str(ROOT),'ls-files','-z']).decode().split('\0')
errors=[]
secret_patterns=[r'gh[pousr]_[A-Za-z0-9]{30,}',r'AKIA[0-9A-Z]{16}',r'eyJ[A-Za-z0-9_-]{20,}\.[A-Za-z0-9_-]{20,}\.[A-Za-z0-9_-]{20,}',r'-----BEGIN (?:RSA |EC |OPENSSH )?PRIVATE KEY-----']
for name in filter(None,files):
    p=ROOT/name;data=p.read_bytes()
    if data.startswith((b'\x7fELF',b'MZ',b'\xcf\xfa\xed\xfe',b'\xca\xfe\xba\xbe')) or b'\0' in data:
        errors.append(name+': binary content is not part of this source package');continue
    try:text=data.decode()
    except UnicodeDecodeError:errors.append(name+': non-UTF8 content');continue
    if re.search(r'/(?:home|Users)/' + 'hi' + 'rew' + r'(?:/|\b)',text):
        errors.append(name+': development-machine path')
    if any(re.search(pattern,text) for pattern in secret_patterns):errors.append(name+': possible credential; review before publishing')
    if name.split('/')[0] in ['build','logs','Config','Library'] or p.suffix in ['.dylib','.so','.asar','.perf','.zip']:
        errors.append(name+': local artifact')
for p in list((ROOT/'scripts').glob('*.py'))+list((ROOT/'build/runtime/tools').glob('*.py')):
    try:ast.parse(p.read_text(),filename=str(p))
    except SyntaxError as e:errors.append(p.name+': '+str(e))
for p in list((ROOT/'scripts').glob('*.sh'))+list((ROOT/'build/runtime/tools').glob('*.sh')):
    result=subprocess.run(['bash','-n',str(p)],capture_output=True,text=True)
    if result.returncode:errors.append(p.name+': shell syntax error')
manifest=json.loads((ROOT/'upstream/manifest.json').read_text())
for c in manifest['components']:
    if not re.fullmatch('[0-9a-f]{40}',c['revision']):errors.append(c['path']+': invalid pinned revision')
    if not (ROOT/c['patch']).is_file():errors.append(c['path']+': patch missing')
if errors:
    print('\n'.join(errors));raise SystemExit(1)
print('PASS: tracked source audit, rendered Python/shell syntax and',len(manifest['components']),'pinned patch entries')
