"""One app/journey selection contract for dispatch, compilation and execution."""
import json


def select(root, app='all', case=None):
    case=case or None
    declarations={json.loads(path.read_text())['id']:path
                  for path in sorted((root/'apps').glob('*/app.json'))}
    allowed=set(declarations) if app=='all' else set(app.split(','))
    if not allowed or not allowed<=set(declarations):
        raise ValueError('Unknown app selection: '+app)
    cases=[];paths={}
    for name in sorted(declarations):
        path=declarations[name].parent/'journeys.json'
        for journey in json.loads(path.read_text())['cases']:
            record={**journey,'id':name+'--'+journey['id'],'owner':name}
            cases.append(record);paths[record['id']]=path
    for path in sorted((root/'apps/scenarios').glob('*.json')):
        journey=json.loads(path.read_text())
        record={**journey,'id':'session--'+journey['id'],'owner':'session'}
        cases.append(record);paths[record['id']]=path
    requested=set(case.split(',')) if case else None
    known={item['id'] for item in cases}
    if requested is not None and not requested<=known:
        raise ValueError('Unknown journey selection: '+','.join(sorted(requested-known)))
    chosen=[];products=set()
    for item in cases:
        needed={step['app'] for step in item['steps'] if 'app' in step}
        if item['owner']!='session':needed.add(item['owner'])
        if not needed<=set(declarations):raise ValueError('Journey references an unknown app: '+item['id'])
        include=item['id'] in requested if requested is not None else (
            item['owner'] in allowed or (item['owner']=='session' and app=='all'))
        if not include:continue
        if not needed<=allowed:raise ValueError('Journey is outside --app selection: '+item['id'])
        chosen.append(item);products.update(needed)
    if not chosen:raise ValueError('No journeys selected')
    chosen.sort(key=lambda item:item['owner']=='network')
    return products,chosen,sorted({paths[item['id']] for item in chosen}),app=='all' and case is None
