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
            'faultContexts':fault_contexts(runtime),
            'observed':witnesses(runtime,('DARWIN_FAULT signal=','DARWIN_NATIVE_FAULT','DARLING_RPC_ERROR ',
                'UI_PLATFORM_IMPORTS_UNAVAILABLE','UI_RUNLOOP_OBSERVER_INSTALLED','UI_LAYER_FRAME sequence='))+
                witnesses(stderr,('IOS_HOST_WINDOW ','WAYLAND_BACKEND_READY ','WAYLAND_TOPLEVEL_FOCUS ',
                    'IOS_RENDER_BINDING_FAILED ','IOS_RENDER_LOCAL_CONTEXT ',
                    'IOS_INPUT_BINDING_FAILED ','IOS_INPUT_KEY_WINDOW ',
                    'IOS_LIFECYCLE_BINDING_FAILED ','IOS_LIFECYCLE_UNSUPPORTED ','IOS_LIFECYCLE_CONTRACT ',
                    'IOS_SCENE_CONSTRUCT_BEGIN ','IOS_SCENE_DELIVER_BEGIN ','IOS_SCENE_DELIVER_RETURNED ',
                    'IOS_SCENE_EXCEPTION ','IOS_SCENE_MESSAGE_UNSUPPORTED ','IOS_SCENE_CLIENT_SETTINGS_RECORDED'))})
    summary['sessionObserved']=witnesses(output/(case+'--session')/'session.log',
        ('SESSION_XPC_REQUEST_UNSUPPORTED ','HIDFOCUS_REQUEST_UNIMPLEMENTED '))
    return summary
