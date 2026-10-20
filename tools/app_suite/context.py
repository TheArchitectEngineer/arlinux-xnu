"""Runner-local shared primitives; no guest implementation."""
import hashlib
from pathlib import Path
import os
import sys
import subprocess
ROOT=Path(__file__).resolve().parents[2]
def command(*args,timeout=10,**kwargs):
    return subprocess.run(list(map(str,args)),check=True,timeout=timeout,**kwargs)
def sha(path): return hashlib.sha256(path.read_bytes()).hexdigest()
def identity(pid):
    if sys.platform=='darwin':
        value=subprocess.check_output(['ps','-p',str(pid),'-o','lstart=','-o','comm='],text=True).strip()
        if not value:raise ProcessLookupError(pid)
        return {'pid':pid,'hostIdentity':value}
    proc=Path('/proc')/str(pid);stat=(proc/'stat').read_text()
    return {'pid':pid,'startTicks':int(stat[stat.rfind(')')+1:].split()[19]),'executable':os.readlink(proc/'exe')}
