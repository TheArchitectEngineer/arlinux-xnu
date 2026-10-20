"""Runner-internal same-run rendezvous; compilation stays in the existing jobs.

The producer publishes its receipt digest in an attempt-specific artifact name.
The next workflow step verifies that receipt and every product before execution.
No source or product is substituted from another run or an older attempt.
"""
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import time

if os.environ.get('GITHUB_ACTIONS')!='true' or sys.platform!='linux':
    raise SystemExit('App product handoff requires the Linux hosted workflow')
repo=os.environ['GITHUB_REPOSITORY'];run_id=os.environ['GITHUB_RUN_ID']
attempt=os.environ['GITHUB_RUN_ATTEMPT'];commit=os.environ['GITHUB_SHA']
prefix=f'ios-app-inputs-{attempt}-'

def api(endpoint):
    for retry in range(3):
        try:
            result=subprocess.run(['gh','api',f'repos/{repo}/{endpoint}'],capture_output=True,timeout=30)
            if not result.returncode:return json.loads(result.stdout)
        except subprocess.TimeoutExpired:
            pass
        if retry<2:time.sleep(3)
    raise RuntimeError('Same-run artifact API unavailable after retries: '+endpoint)

started=time.monotonic();deadline=started+600
producer_finished=None
while True:
    entries=api(f'actions/runs/{run_id}/artifacts?per_page=100')['artifacts']
    matches=[item for item in entries if re.fullmatch(re.escape(prefix)+r'[0-9a-f]{64}',item['name'])
             and not item['expired'] and item.get('workflow_run',{}).get('head_sha')==commit]
    if len(matches)>1:raise SystemExit('Ambiguous same-run app artifacts')
    if matches:
        artifact=matches[0];break
    jobs=api(f'actions/runs/{run_id}/attempts/{attempt}/jobs?per_page=100')['jobs']
    producer=next((job for job in jobs if job['name']=='apps'),None)
    if producer and producer['status']=='completed':
        if producer_finished is None:producer_finished=time.monotonic()
        if time.monotonic()-producer_finished>=15:
            raise SystemExit('App producer completed without its current-attempt artifact: '+str(producer['conclusion']))
    if time.monotonic()>=deadline:raise SystemExit('Timed out waiting for current-attempt app products')
    time.sleep(3)
destination=Path(__file__).resolve().parents[1]/'build/apps'
destination.mkdir(parents=True,exist_ok=False)
subprocess.run(['gh','run','download',run_id,'--repo',repo,'--name',artifact['name'],
                '--dir',str(destination)],check=True,timeout=120)
digest=artifact['name'][len(prefix):]
with open(os.environ['GITHUB_OUTPUT'],'a') as stream:stream.write('receipt='+digest+'\n')
print('APP_INPUT_HANDOFF '+json.dumps({'artifactId':artifact['id'],'runId':run_id,'attempt':attempt,
    'commit':commit,'receiptSHA256':digest,'waitAndDownloadSeconds':round(time.monotonic()-started,3)}),flush=True)
