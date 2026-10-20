#!/usr/bin/env python3
"""Run independent source-owned app journeys. Public entry: build.sh."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import subprocess
import sys
import time
from app_suite.case import Case
from app_suite.context import ROOT
from app_suite.session import Session,AppleSession
from app_suite.feedback import failure_summary

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--app',default='all');parser.add_argument('--reference',action='store_true');args=parser.parse_args()
    expected='darwin' if args.reference else 'linux'
    if sys.platform!=expected or platform.machine() not in ('aarch64','arm64') or os.environ.get('GITHUB_ACTIONS')!='true':
        raise SystemExit('App execution requires its ARM64 GitHub runner through build.sh')
    products=json.loads((ROOT/'build/apps/products.json').read_text())
    if products['source']!=os.environ['GITHUB_SHA'] or products['runId']!=os.environ['GITHUB_RUN_ID']:
        raise SystemExit('App products do not belong to this exact source/run')
    apps={app['id']:app for app in products['apps']}
    declared={json.loads(path.read_text())['id'] for path in (ROOT/'apps').glob('*/app.json')}
    selected=declared if args.app=='all' else set(args.app.split(','))
    if not selected or not selected<=declared:parser.error('Unknown app selection')
    if not selected<=set(apps):raise SystemExit('Requested apps are missing from the verified build selection')
    output=ROOT/'build/results/apps';output.mkdir(parents=True,exist_ok=False)
    cases=[];inputs=[]
    for app in sorted(selected,key=lambda app:(app=="network",app)):
        path=ROOT/'apps'/app/'journeys.json';inputs.append(path)
        for case in json.loads(path.read_text())['cases']:cases.append({**case,'id':app+'--'+case['id'],'owner':app})
    if args.app=='all':
        for path in sorted((ROOT/'apps/scenarios').glob('*.json')):
            inputs.append(path);case=json.loads(path.read_text());cases.append({**case,'id':'session--'+case['id'],'owner':'session'})
    cases.sort(key=lambda case:case['owner']=='network')
    results=[]
    for case in cases:
        started=time.monotonic()
        try:
            with (AppleSession if args.reference else Session)(output/(case['id']+'--session')) as session:
                result=Case(case,apps,session.identity,session.environment,output,args.reference).run()
        except (OSError,RuntimeError,subprocess.SubprocessError) as error:
            result={'id':case['id'],'status':'failed','error':str(error),'steps':[], 'infrastructureFailure':True}
        result['journeySeconds']=round(time.monotonic()-started,3)
        if result['status']=='failed':
            try:result['failureEvidence']=failure_summary(result,output)
            except (OSError,ValueError,KeyError) as error:
                result['failureEvidence']={'id':case['id'],'summaryUnavailable':str(error)}
        results.append(result)
        (output/(case['id']+'.json')).write_text(json.dumps(result,indent=2)+'\n')
        print('APP_CASE '+json.dumps({'id':case['id'],'status':result['status'],'error':result.get('error')}),flush=True)
        if 'failureEvidence' in result:
            print('APP_DIAGNOSIS '+json.dumps(result['failureEvidence']),flush=True)
    result={'source':products['source'],'runId':products['runId'],'attempt':os.environ['GITHUB_RUN_ATTEMPT'],
        'backend':'apple-host' if args.reference else 'linux-darling-wayland','selection':args.app,'fullSuite':args.app=='all','deviceAcceptance':False,
        'inputs':{str(path.relative_to(ROOT)):hashlib.sha256(path.read_bytes()).hexdigest() for path in inputs},
        'passed':bool(results) and all(case['status']=='passed' for case in results),'cases':results}
    (output/'result.json').write_text(json.dumps(result,indent=2)+'\n')
    print('APP_SUITE '+json.dumps({k:result[k] for k in ('passed','fullSuite','selection')}),flush=True)
    return 0 if result['passed'] else 1
if __name__=='__main__':raise SystemExit(main())
