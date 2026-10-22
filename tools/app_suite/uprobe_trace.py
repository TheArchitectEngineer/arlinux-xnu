"""Runner-internal, optional kernel observation of original Mach-O execution.

Uses a private tracefs instance; never clears another tracer or changes a guest
result. Public entry remains build.sh. No extra runner package is needed.
Linux contract: docs.kernel.org/trace/uprobetracer.html.
"""
import hashlib
import errno
import json
import os
from pathlib import Path
import subprocess
import sys
import time
from original_trace import symbol_address

TRACE=Path('/sys/kernel/tracing')


def save(path,value):path.write_text(json.dumps(value,indent=2)+'\n')


def error_log():
    try:return (TRACE/'error_log').read_text()[-8192:]
    except OSError:return 'kernel error log unavailable'


def control(command):
    # Python append-mode FileIO performs SEEK_END, which tracefs control files
    # reject. One write is one kernel command; never truncate the global list.
    fd=os.open(TRACE/'uprobe_events',os.O_WRONLY|os.O_APPEND|os.O_CLOEXEC)
    try:
        data=(command+'\n').encode()
        if os.write(fd,data)!=len(data):raise OSError(errno.EIO,'incomplete tracefs command')
    finally:os.close(fd)


def remove_events(group,events):
    for event in reversed(events):
        control(f'-:{group}/{event}')


def start(specification,state_path):
    state={'status':'configuring','events':[],'group':'arlinux_'+str(os.getpid())+'_'+str(time.time_ns())}
    instance=TRACE/'instances'/state['group']
    save(state_path,state)
    try:
        if not (TRACE/'uprobe_events').exists():
            subprocess.run(['mount','-t','tracefs','tracefs',str(TRACE)],check=True,capture_output=True,timeout=5)
        instance.mkdir()
        (instance/'tracing_on').write_text('0')
        (instance/'trace_clock').write_text('mono')
        state['clock']=(instance/'trace_clock').read_text()
        (instance/'buffer_size_kb').write_text('128')
        # Record only the requested file-backed original code; no system-wide
        # syscall/scheduler catalogue or payload/string/heap capture.
        records=json.loads(specification.read_text())
        if not 1<=len(records)<=8:raise ValueError('original symbol budget exceeded')
        state['symbols']=[]
        for index,record in enumerate(records):
            image=Path(record['path']).resolve()
            if not image.is_file() or any(c.isspace() or c==':' for c in str(image)):
                raise ValueError('unsupported uprobe image path')
            offset=symbol_address(image,record['symbol'],file_offset=True)
            if offset%4 or offset+4>image.stat().st_size:raise ValueError('uprobe offset outside original image')
            state['symbols'].append({**record,'fileOffset':hex(offset),
                'sha256':hashlib.sha256(image.read_bytes()).hexdigest()})
            for kind,suffix in (('p','entry'),('r','return')):
                name=f'point{index}_{suffix}_{state["group"]}'
                fetch=' receiver=%x20:x64 arg0=%x0:x64 arg1=%x1:x64 caller=%x30:x64 stack=$stack:x64' if kind=='p' else ' result=$retval:x64'
                definition=f'{kind}:{state["group"]}/{name} {image}:{offset:#x}'
                state['lastCommand']=definition+fetch
                try:
                    control(definition+fetch)
                except OSError as error:
                    if error.errno!=errno.EINVAL:raise
                    state.setdefault('fetchUnavailable',[]).append(dict(command=definition+fetch,error=error_log()))
                    # Counts/timestamps can answer an entry-order question;
                    # unavailable register values must never be inferred.
                    state['lastCommand']=definition
                    control(definition)
                state['events'].append(name)
                save(state_path,state)
        (instance/'events'/state['group']/'enable').write_text('1')
        (instance/'tracing_on').write_text('1')
        state['startedMonotonicNs']=time.monotonic_ns()
        state['status']='active'
    except (OSError,ValueError,subprocess.SubprocessError) as error:
        state['status']='unavailable'
        state['error']=str(error)
        state['kernelError']=error_log()
        try:
            if instance.exists():
                (instance/'tracing_on').write_text('0')
                if (instance/'events'/state['group']/'enable').exists():
                    (instance/'events'/state['group']/'enable').write_text('0')
            remove_events(state['group'],state['events'])
            if instance.exists():instance.rmdir()
        except OSError as cleanup_error:state['cleanupError']=str(cleanup_error)
    save(state_path,state)
    print('ORIGINAL_KERNEL_TRACE '+json.dumps(state),flush=True)


def stop(state_path):
    state=json.loads(state_path.read_text());group=state['group']
    if not group.startswith('arlinux_') or not all(c.isalnum() or c=='_' for c in group):
        raise ValueError('invalid owned trace instance')
    if state['status'] not in ('active','configuring'):return
    instance=TRACE/'instances'/group
    try:
        (instance/'tracing_on').write_text('0')
        state['stoppedMonotonicNs']=time.monotonic_ns()
        (instance/'events'/group/'enable').write_text('0')
        state_path.with_suffix('.log').write_text((instance/'trace').read_text())
        state['cpuStatistics']={p.parent.name:p.read_text() for p in (instance/'per_cpu').glob('cpu*/stats')}
        state['profile']=[line for line in (TRACE/'uprobe_profile').read_text().splitlines() if group in line]
        state['status']='recorded'
    except OSError as error:state['error']=str(error);state['status']='collection-failed'
    finally:
        try:remove_events(group,state['events']);instance.rmdir()
        except OSError as error:state['cleanupError']=str(error)
        save(state_path,state)
        print('ORIGINAL_KERNEL_TRACE '+json.dumps(state),flush=True)


if __name__=='__main__':
    if sys.platform!='linux' or os.geteuid()!=0 or os.environ.get('GITHUB_ACTIONS')!='true':
        raise SystemExit('Kernel observation requires the hosted Linux runner through build.sh')
    action= sys.argv[1]
    if action=='start':start(Path(sys.argv[2]),Path(sys.argv[3]))
    elif action=='stop':stop(Path(sys.argv[2]))
    else:raise SystemExit('unknown kernel observation action')
