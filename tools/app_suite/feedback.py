"""Bounded factual failure summary over the original journey's own evidence.
No cause classification, guest calls, image recognition or acceptance decisions.
"""
import json
import re


def fault_contexts(runtime):
    """Summarize only actual fault PCs; nearby literals are context, not causes."""
    path=runtime.with_suffix('.symbols.json')
    if not runtime.is_file() or not path.is_file():return []
    faults={int(value,16) for value in re.findall(r'DARWIN_FAULT signal=\S+ pc=(0x[0-9a-fA-F]+)',runtime.read_text(errors='replace'))}
    records=json.loads(path.read_text())
    result=[]
    for record in records:
        context=record.get('faultContext')
        if not context or int(record.get('pc','0'),16) not in faults:continue
        result.append({'pc':record['pc'],'image':record['image'],
            'nearestSymbol':record.get('nearestSymbol'),'displacement':record.get('displacement'),
            'contextBytes':context.get('bytes'),'limitedByByteBudget':context.get('limitedByByteBudget'),
            'referencedLiterals':context.get('referencedLiterals',[])[:8],
            'evidence':path.name,'note':'Original fault instruction context; literals are not a root-cause verdict'})
    return result

def witnesses(path,prefixes):
    found={}
    if path.is_file():
        with path.open(errors='replace') as stream:
            for line in stream:
                for prefix in prefixes:
                    if line.startswith(prefix):found[prefix]=line.strip()[:512]
    return list(found.values())


def startup_trace(path,output):
    if not path.is_file():return None
    summary={'log':str(path.relative_to(output)),'hits':[],'unhit':[],'collectorErrors':[]}
    for line in path.read_text(errors='replace').splitlines():
        if line.startswith('ORIGINAL_TRACE_HIT '):
            summary['hits'].append(json.loads(line.split(' ',1)[1]))
        elif line.startswith('ORIGINAL_TRACE_WINDOW_ENDED '):
            summary['unhit']=json.loads(line.split(' ',1)[1])['unhit']
        elif line.startswith(('ORIGINAL_TRACE_UNAVAILABLE ','STARTUP_TRACE_UNAVAILABLE ','ORIGINAL_TRACE_UNEXPECTED_STOP ')):
            summary['collectorErrors'].append(line[:512])
        elif line.startswith('ORIGINAL_TRACE_DETACH '):summary['detach']=line.split(' ',1)[1]
        elif line.startswith('STARTUP_TRACE_EXIT status='):summary['exitStatus']=int(line.split('=',1)[1])
    summary['note']='Bounded debugger observations; unhit is not proof of non-execution outside the window'
    apple=path.with_name(path.name.replace('.startup.lldb.log','.apple-startup.json'))
    if apple.is_file():
        observations=json.loads(apple.read_text())
        summary['appleEvidence']=str(apple.relative_to(output))
        summary['appleStops']=[{'pid':entry['pid'],'memoryMap':entry.get('memoryMap'),
            'threads':[{'id':thread['id'],'reason':thread['reason'],'frames':thread['frames'][:4]}
                for thread in entry.get('threads',[]) if thread.get('reason')][:2]}
            for entry in observations if entry.get('event')=='actual-stop'][:2]
        summary['appleDetach']=[entry for entry in observations if entry.get('event')=='detach']
    return summary

def unimplemented_calls(runtime):
    """Observed ENOSYS counts and bounded original callers, not cause ranking."""
    if not runtime.is_file():return {}
    text=runtime.read_text(errors='replace');counts={};callers={};active={}
    path=runtime.with_suffix('.symbols.json')
    symbols={item['pc']:item for item in json.loads(path.read_text()) if 'pc' in item} if path.is_file() else {}
    for line in text.splitlines():
        call=re.match(r'DARWIN_CALL nr=(0x[0-9a-f]+) pc=\S+ result=0xffffffffffffffda\b',line)
        if call:
            number=int(call[1],16);counts[number]=counts.get(number,0)+1
        start=re.match(r'DARWIN_UNIMPLEMENTED nr=(0x[0-9a-f]+).* tid=(0x[0-9a-f]+)',line)
        if start:active[start[2]]=int(start[1],16)
        frame=re.match(r'DARWIN_UNIMPLEMENTED_FRAME nr=\S+ pc=(0x[0-9a-f]+).* tid=(0x[0-9a-f]+)',line)
        if frame and frame[2] in active:
            frames=callers.setdefault(active[frame[2]],[])
            if len(frames)<4:
                pc=hex(int(frame[1],16));record=symbols.get(pc,{})
                frames.append({'pc':pc,**{key:record[key] for key in ('image','nearestSymbol','displacement') if key in record}})
    selected=sorted(counts,key=lambda number:(-counts[number],number))[:8]
    paths=list(dict.fromkeys(re.findall(r'^DARWIN_GETATTRLIST_PATH (/[^\n]+)',text,re.MULTILINE)))
    return {'distinctSyscalls':len(counts),'calls':[
        {'number':number,'count':counts[number],'originalCallers':callers.get(number,[])} for number in selected],
        'omittedSyscalls':max(0,len(counts)-len(selected)),
        'getattrlistPaths':paths[-8:],'getattrlistPathsOmitted':max(0,len(paths)-8),
        'note':'ENOSYS frequency is observation, not proof of the failed effect cause; full arguments and callers remain in runtimeLog'}


def failure_summary(result,output):
    case=result['id'];directory=output/case
    failed=next((step for step in result.get('steps',[]) if step['status']=='failed'),{})
    screenshots=[entry for entry in result.get('observations',[]) if 'screenshot' in entry]
    sample=screenshots if len(screenshots)<=4 else screenshots[:2]+screenshots[-2:]
    summary={'id':case,'failedStep':{key:failed[key] for key in
        ('do','app','text','reason','seconds','diagnosticSeconds','observedApplicationState') if key in failed},
        'blockedSteps':sum(step['status']=='blocked' for step in result.get('steps',[])),
        'infrastructureFailure':result.get('infrastructureFailure',False),
        'processes':[],'screenshots':sample,'screenshotsRecorded':len(screenshots),
        'screenshotsOmittedFromSummary':len(screenshots)-len(sample),'fullJourney':case+'.json',
        'diagnostic':result.get('diagnostic')}
    for generation in result.get('processes',[]):
        prefix=f"{generation['app']}-{generation['generation']}"
        runtime=directory/(prefix+'.runtime.log');stderr=directory/(prefix+'.stderr')
        summary['processes'].append({'app':generation['app'],'generation':generation['generation'],
            'identity':generation['identity'],
            'runtimeLog':str(runtime.relative_to(output)),
            'stderr':str(stderr.relative_to(output)),
            'startupTrace':startup_trace(directory/(prefix+'.startup.lldb.log'),output),
            'faultContexts':fault_contexts(runtime),
            'unimplementedSyscalls':unimplemented_calls(runtime),
            'observed':witnesses(runtime,('DARWIN_FAULT signal=','DARWIN_NATIVE_FAULT','DARLING_RPC_ERROR ',
                'UI_PLATFORM_IMPORTS_UNAVAILABLE','UI_RUNLOOP_OBSERVER_INSTALLED','UI_LAYER_FRAME sequence='))+
                witnesses(stderr,('IOS_HOST_WINDOW ','WAYLAND_BACKEND_READY ','WAYLAND_TOPLEVEL_FOCUS ',
                    'Assertion failed: ',
                    'IOS_RENDER_BINDING_FAILED ','IOS_RENDER_LOCAL_CONTEXT ',
                    'IOS_RENDER_CPU_BACKING ','IOS_RENDER_CPU_BACKING_FAILED ',
                    'IOS_INPUT_BINDING_FAILED ','IOS_INPUT_KEY_WINDOW ','IOS_INPUT_KEYBOARD_BACKEND ',
                    'IOS_COLOR_LOOKUP_FAILED ','IOS_COLOR_TRACE_UNAVAILABLE ',
                    'IOS_LIFECYCLE_BINDING_FAILED ','IOS_LIFECYCLE_UNSUPPORTED ','IOS_LIFECYCLE_CONTRACT ',
                    'IOS_SCENE_CONSTRUCT_BEGIN ','IOS_SCENE_DELIVER_BEGIN ','IOS_SCENE_DELIVER_RETURNED ',
                    'IOS_UNCAUGHT_EXCEPTION ','IOS_SCENE_EXCEPTION ','IOS_SCENE_MESSAGE_UNSUPPORTED ','IOS_SCENE_CLIENT_SETTINGS_RECORDED',
                    'IOS_FILE_COORDINATION ','IOS_FILE_COORDINATION_TRACE_UNAVAILABLE ',
                    'IOS_REPLACEMENT_DIRECTORY ','IOS_REPLACEMENT_DIRECTORY_BINDING_FAILED '))})
    summary['sessionObserved']=witnesses(output/(case+'--session')/'session.log',
        ('SESSION_XPC_REQUEST_UNSUPPORTED ','HIDFOCUS_REQUEST_UNIMPLEMENTED ',
         'SESSION_SERVICE_BUNDLE_CONFLICT ','SESSION_SERVICE_SPAWN_FAILED ',
         'SESSION_SERVICE_STARTED ','SESSION_SERVICE_EXITED ',
         'SESSION_SERVICE_PENDING ','SESSION_SERVICE_RECEIVE_GRANT ',
         'NATIVE_SERVICE_INSTANCE ','SESSION_NATIVE_FAULT ','DARLING_RPC_ERROR ','DARLING_RPC_REPLY_SAVED ','ERROR '))
    return summary
