from pathlib import Path
import getpass,json,os
ROOT=Path(__file__).resolve().parent.parent

def configuration():
    file=ROOT/'build/config.json'
    if file.exists():return json.loads(file.read_text())
    username=getpass.getuser()
    return {'project_root':str(ROOT),'host_home':str(Path.home()),'prefix':str(Path.home()/'.darling'),'guest_user':username,'guest_home':'/Users/'+username,'sdk':'/usr/local/libexec/darling'}

def render(text,config=None):
    c=config or configuration()
    for token,key in [('PROJECT_ROOT','project_root'),('HOST_HOME','host_home'),('DPREFIX','prefix'),('GUEST_USER','guest_user'),('GUEST_HOME','guest_home'),('DARLING_SDK','sdk')]:
        text=text.replace('@'+token+'@',c[key])
    return text
