"""Each independent journey owns a fresh actual shared Mach session."""
import os
from pathlib import Path
import signal
import subprocess
import tempfile
import time
from .context import ROOT,identity
class Session:
    def __init__(self,directory):self.directory=directory
    def __enter__(self):
        self.directory.mkdir(parents=True,exist_ok=False)
        self.temporary=tempfile.TemporaryDirectory(prefix='arlinux-app-session-')
        self.log=(self.directory/'session.log').open('wb')
        self.process=subprocess.Popen([str(ROOT/'build/native/macho-native'),'--session',self.temporary.name],
            stdout=self.log,stderr=subprocess.STDOUT,start_new_session=True)
        try:
            deadline=time.monotonic()+15
            while 'SESSION_READY ' not in (self.directory/'session.log').read_text(errors='replace'):
                if self.process.poll() is not None:raise RuntimeError('Shared session exited before readiness')
                if time.monotonic()>=deadline:raise RuntimeError('Shared session startup timed out')
                time.sleep(.05)
            self.identity=identity(self.process.pid)
            self.environment={**os.environ,'ARLINUX_DARLING_PID':str(self.process.pid),
                'ARLINUX_SESSION_PID':str(self.process.pid),'ARLINUX_DARLING_PREFIX':self.temporary.name}
            return self
        except BaseException:
            self.__exit__(None,None,None);raise
    def __exit__(self,*args):
        if self.process.poll() is None:
            os.killpg(self.process.pid,signal.SIGTERM)
            try:self.process.wait(timeout=3)
            except subprocess.TimeoutExpired:
                os.killpg(self.process.pid,signal.SIGKILL);self.process.wait(timeout=3)
        self.log.close();self.temporary.cleanup()

class AppleSession:
    """Observe actual host launchd; it is not our Linux shared-session implementation."""
    def __init__(self,directory):self.directory=directory
    def __enter__(self):
        self.directory.mkdir(parents=True,exist_ok=False)
        self.identity=identity(1);self.environment=os.environ.copy();return self
    def __exit__(self,*args):pass
