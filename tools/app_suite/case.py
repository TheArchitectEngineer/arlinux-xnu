import os
from pathlib import Path
import re
import signal
import subprocess
import sys
import time
from .context import ROOT,command,sha,identity
from .processes import Processes
from .desktop import Desktop
from .network import HttpFixture

class Case(Processes,Desktop):
    def __init__(self, case, apps, session, environment, output, reference=False):
        self.case, self.apps, self.session = case, apps, session
        self.reference=reference
        self.output=output
        self.environment=environment
        self.http=None
        self.painted={}
        self.directory = output / case['id']
        self.directory.mkdir(parents=True, exist_ok=False)
        self.processes, self.streams, self.generations = {}, [], []
        self.steps, self.observations, self.cleanup = [], [], []
        self.current = None
        self.helpers = []

    def step(self, step):
        action, name = step['do'], step.get('app', self.current)
        if action == 'launch':
            self.launch(name)
        elif action == 'stdout':
            if self.apps[name]['kind']!='console': raise RuntimeError('GUI effects require composed pixels')
            item=self.processes[name]; deadline=time.monotonic()+8
            while step['text'] not in Path(item['stdout']).read_text(errors='replace'):
                self.guard()
                if time.monotonic()>=deadline: raise RuntimeError('Console output missing: '+step['text'])
                time.sleep(.05)
        elif action == 'stdin':
            stream=self.processes[name]['process'].stdin
            stream.write(step['text'].encode());stream.flush()
        elif action == 'http-start':
            self.http=HttpFixture(self.directory);self.http.start()
        elif action == 'http-request':
            minimum=step.get('minimum',1)
            if type(minimum) is not int or minimum<1:raise RuntimeError('HTTP request minimum must be a positive integer')
            count=sum(item['path']=='/value.json' for item in self.http.requests) if self.http else 0
            self.steps[-1]['requestsObserved']=count
            if not self.http or not self.http.received(minimum):
                raise RuntimeError(f'HTTP peer received {count} matching requests; required {minimum}')
        elif action == 'pixels':
            self.observe_pixels(step)
        elif action == 'see':
            self.observe(step['text'],timeout_seconds=step.get('timeoutSeconds',8),region=step.get('region'))
        elif action == 'application-state':
            deadline=time.monotonic()+8
            while True:
                self.guard();observed=self.inspection(name)
                self.steps[-1]['observedApplicationState']={
                    'app':name,'identity':self.processes[name]['identity'],
                    **({key:observed.get(key) for key in ('applicationState','activationNotifications')} if observed else {})}
                if observed and observed.get('applicationState')==step['state'] and all(
                    observed.get('activationNotifications',{}).get(key,0)>=minimum
                    for key,minimum in step.get('notifications',{}).items()):break
                if time.monotonic()>=deadline:
                    self.diagnostic_target=name
                    raise RuntimeError('Application state/notifications did not reach '+name+' '+step['state'])
                time.sleep(.1)
        elif action == 'click':
            target_kind=step.get('target','control')
            if target_kind not in ('control','text'):raise RuntimeError('Unknown click target kind')
            target = self.observe(step['text'], word=target_kind=='control',region=step.get('region'))
            self.click_target(target)
        elif action == 'focus':
            if self.reference:
                command(ROOT/'build/reference-tools/desktop-events','focus',self.processes[name]['process'].pid)
                self.current=name
                return
            deadline=time.monotonic()+8
            while True:
                self.guard()
                log=Path(self.processes[name]['stdout']).with_suffix('.stderr').read_text(errors='replace')
                events=re.findall(r'WAYLAND_TOPLEVEL_FOCUS[^\n]*activated=(0|1)',log)
                if events and events[-1]=='1':self.current=name;break
                if time.monotonic()>=deadline:raise RuntimeError('Actual compositor did not focus requested app')
                # The workflow runs Weston's default desktop shell with
                # --no-config: its window switcher is Super+Tab, not Alt+Tab.
                command('xdotool','key','--clearmodifiers','super+Tab');time.sleep(.2)
        elif action == 'type':
            if self.reference: command(ROOT/'build/reference-tools/desktop-events','type',step['text'])
            else: command('xdotool', 'type', '--clearmodifiers', '--delay', '40', step['text'])
        elif action == 'close':
            self.processes[name]['expectedExit'] = True
            if self.reference: command(ROOT/'build/reference-tools/desktop-events','close',self.processes[name]['process'].pid)
            else: command('xdotool', 'key', '--clearmodifiers', 'alt+F4')
        elif action == 'exit':
            item = self.processes[name]
            item['expectedExit'] = True
            status = item['process'].wait(timeout=5)
            if status != 0:
                raise RuntimeError(f'Normal close returned {status}, expected 0')
            self.steps[-1]['exitStatus'] = status
        elif action == 'alive':
            item = self.processes[name]
            if item['process'].poll() is not None or identity(item['process'].pid) != item['identity']:
                raise RuntimeError('Peer identity/lifetime was disrupted: ' + name)
        elif action == 'helper':
            matches = self.helper_processes(name)
            if len(matches) != 1 or matches[0] == self.processes[name]['identity']:
                raise RuntimeError('No unique independent extension helper in this compatibility session')
            if any(old['identity'] == matches[0] for old in self.helpers):
                raise RuntimeError('Extension relaunch reused the old helper generation')
            record = {'app': name, 'identity': matches[0], 'binarySHA256': sha(ROOT / self.apps[name]['helper'])}
            self.helpers.append(record)
            self.steps[-1]['helper'] = record
        elif action == 'helper-exit':
            deadline = time.monotonic() + 5
            while self.helper_processes(name):
                if time.monotonic() >= deadline:
                    raise RuntimeError('Extension helper survived normal application close')
                time.sleep(.1)
        elif action == 'crash':
            item = self.processes[name]
            item['expectedExit'] = True
            os.kill(item['process'].pid, signal.SIGKILL)
            if item['process'].wait(timeout=5) != -signal.SIGKILL:
                raise RuntimeError('Requested crash was not observed')
            time.sleep(.4)
        else:
            raise RuntimeError('Unknown journey action: ' + action)

    def run(self):
        error = None
        self.diagnostic = None
        try:
            for step in self.case['steps']:
                if error:
                    self.steps.append({**step, 'status': 'blocked', 'reason': 'Earlier required step failed'})
                    continue
                self.steps.append({**step, 'status': 'running'})
                started = time.monotonic()
                try:
                    if step['do']=='exit':
                        self.processes[step.get('app',self.current)]['expectedExit']=True
                    self.guard()
                    self.step(step)
                    self.steps[-1]['status'] = 'passed'
                except (RuntimeError, OSError, subprocess.SubprocessError) as failure:
                    error = str(failure)
                    self.steps[-1].update(status='failed', reason=error,seconds=round(time.monotonic()-started,3))
                    diagnosis_started=time.monotonic()
                    failures=[]
                    for collect in (lambda:self.snapshot('failure'),self.diagnose):
                        try: collect()
                        except (RuntimeError,OSError,subprocess.SubprocessError) as diagnostic_error:
                            failures.append(str(diagnostic_error))
                    if failures:self.diagnostic={'collectionErrors':failures,'originalProcess':self.diagnostic}
                    self.steps[-1]['diagnosticSeconds']=round(time.monotonic()-diagnosis_started,3)
                if 'seconds' not in self.steps[-1]:
                    self.steps[-1]['seconds'] = round(time.monotonic() - started, 3)
        finally:
            if self.http: self.http.stop()
            for name, item in self.processes.items():
                process = item['process']
                forced = process.poll() is None
                if forced:
                    try:
                        os.killpg(process.pid, signal.SIGTERM)
                    except ProcessLookupError:
                        pass
                    try:
                        process.wait(timeout=2)
                    except subprocess.TimeoutExpired:
                        os.killpg(process.pid, signal.SIGKILL)
                        process.wait(timeout=2)
                self.cleanup.append({'app': name, 'identity': item['identity'], 'forced': forced,
                                     'status': process.returncode, 'acceptanceEvidence': False})
            for item in self.processes.values():
                if item['process'].stdin: item['process'].stdin.close()
            for stream in self.streams:
                stream.close()
            for name, app in self.apps.items():
                if 'helper' not in app:
                    continue
                for helper in ([] if self.reference else self.helper_processes(name)):
                    try:
                        os.kill(helper['pid'], signal.SIGKILL)
                    except ProcessLookupError:
                        pass
                    self.cleanup.append({'app': name, 'helper': helper, 'forced': True,
                                         'acceptanceEvidence': False})
        return {'id': self.case['id'], 'status': 'failed' if error else 'passed', 'error': error,
                'session': self.session, 'processes': self.generations, 'steps': self.steps,
                'observations': self.observations, 'helpers': self.helpers,
                'diagnostic': self.diagnostic, 'cleanup': self.cleanup}
