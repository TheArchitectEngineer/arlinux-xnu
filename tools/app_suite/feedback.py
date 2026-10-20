"""Bounded factual failure summary over the original journey's own evidence.
No cause classification, guest calls, image recognition or acceptance decisions.
"""

def witnesses(path,prefixes):
    found={}
    if path.is_file():
        with path.open(errors='replace') as stream:
            for line in stream:
                for prefix in prefixes:
                    if line.startswith(prefix):found[prefix]=line.strip()[:512]
    return list(found.values())

def failure_summary(result,output):
    case=result['id'];directory=output/case
    failed=next((step for step in result.get('steps',[]) if step['status']=='failed'),{})
    summary={'id':case,'failedStep':{key:failed[key] for key in
        ('do','app','text','reason','seconds','diagnosticSeconds') if key in failed},
        'blockedSteps':sum(step['status']=='blocked' for step in result.get('steps',[])),
        'infrastructureFailure':result.get('infrastructureFailure',False),
        'processes':[],'screenshots':[entry for entry in result.get('observations',[]) if 'screenshot' in entry],
        'diagnostic':result.get('diagnostic')}
    for generation in result.get('processes',[]):
        prefix=f"{generation['app']}-{generation['generation']}"
        runtime=directory/(prefix+'.runtime.log');stderr=directory/(prefix+'.stderr')
        summary['processes'].append({'app':generation['app'],'generation':generation['generation'],
            'identity':generation['identity'],
            'runtimeLog':str(runtime.relative_to(output)),
            'stderr':str(stderr.relative_to(output)),
            'observed':witnesses(runtime,('DARWIN_FAULT signal=','DARWIN_NATIVE_FAULT',
                'UI_PLATFORM_IMPORTS_UNAVAILABLE','UI_RUNLOOP_OBSERVER_INSTALLED','UI_LAYER_FRAME sequence='))+
                witnesses(stderr,('IOS_HOST_WINDOW ','WAYLAND_BACKEND_READY ','WAYLAND_TOPLEVEL_FOCUS '))})
    summary['sessionObserved']=witnesses(output/(case+'--session')/'session.log',
        ('SESSION_XPC_REQUEST_UNSUPPORTED ','HIDFOCUS_REQUEST_UNIMPLEMENTED '))
    return summary
