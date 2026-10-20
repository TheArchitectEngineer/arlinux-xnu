#!/usr/bin/env python3
"""Dispatch and inspect the hosted XNU experiment; never compile locally."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import time
from datetime import datetime


REPO = 'taowen/arlinux-xnu'
ROOT = Path(__file__).resolve().parents[1]


def verify_result_files(directory, expected, run):
    data=(directory/'receipt.json').read_bytes()
    if hashlib.sha256(data).hexdigest()!=expected:
        raise ValueError('Receipt digest differs from the exact producing job log')
    receipt=json.loads(data)
    if (receipt['repository'],receipt['commit'],str(receipt['runId']),str(receipt['runAttempt']))!=(
            REPO,run['commit'],str(run['runId']),str(run['attempt'])):
        raise ValueError('Evidence source/run/attempt mismatch')
    seen=set()
    for entry in receipt['files']:
        relative=entry['path'];path=directory/relative
        if ('\\' in relative or ':' in relative or relative.startswith('/') or
                any(part in ('','..','.') for part in relative.split('/')) or
                not path.resolve().is_relative_to(directory.resolve()) or path.is_symlink()):
            raise ValueError('Unsafe evidence path')
        if relative.casefold() in seen:raise ValueError('Duplicate evidence path')
        seen.add(relative.casefold())
        if path.stat().st_size!=entry['bytes'] or hashlib.sha256(path.read_bytes()).hexdigest()!=entry['sha256']:
            raise ValueError('Evidence integrity mismatch: '+relative)
    return len(seen)


def collect_evidence(run):
    for job in run['jobs']:
        artifact={'native-userland':'ios-app-results-linux','apple-reference':'ios-app-results-apple'}.get(job['name'])
        if not artifact or job['status']!='completed' or job['conclusion']=='skipped':continue
        receipts=[json.loads(line[len('EXPERIMENT_RECEIPT '):]) for line in job.get('observations',[])
                  if line.startswith('EXPERIMENT_RECEIPT ')]
        matches=[receipt for receipt in receipts if receipt['directory']=='build/results'
                 and receipt['commit']==run['commit'] and str(receipt['runId'])==str(run['runId'])]
        if len(matches)!=1:
            print(f'  {artifact}: producing receipt unavailable; no evidence verified')
            continue
        directory=ROOT/'build/runs'/str(run['runId'])/f"attempt-{run['attempt']}"/artifact
        expected=matches[0]['receiptSHA256']
        if not directory.exists():
            directory.parent.mkdir(parents=True,exist_ok=True)
            subprocess.run(['gh','run','download',str(run['runId']),'--repo',REPO,
                '--name',artifact,'--dir',str(directory)],check=True,timeout=120)
        try:count=verify_result_files(directory,expected,run)
        except (OSError,ValueError,KeyError) as error:
            raise SystemExit('Evidence verification failed: '+str(error)) from error
        print(f'  Verified {artifact}: receipt plus {count} files; {directory}')


def wait_for_run(run_id):
    previous=None
    while True:
        try:run=json.loads(api('actions/runs/'+run_id))
        except SystemExit:
            print(f'Observation unavailable; keep following run {run_id}, no redispatch',flush=True)
            time.sleep(30)
            continue
        status=(run['status'],run['conclusion'])
        if status!=previous:
            print(f'Run {run_id}: {status[0]} / {status[1]}',flush=True);previous=status
        if run['status']=='completed':return
        time.sleep(30)


def api(endpoint):
    result = subprocess.run(['gh', 'api', '--allow-escape-sequences', 'repos/' + REPO + '/' + endpoint],
                            capture_output=True, timeout=60)
    if result.returncode:
        # Redirect failures can include signed download URLs in stderr.
        raise SystemExit('GitHub API request failed: ' + endpoint)
    return result.stdout


def report(run_id, job_name, verbose=False):
    """Read existing execution evidence; never dispatch or execute a guest."""
    run = json.loads(api('actions/runs/' + run_id))
    attempt = run['run_attempt']
    endpoint = f'actions/runs/{run_id}/attempts/{attempt}/jobs'
    jobs, page = [], 1
    while True:
        batch = json.loads(api(endpoint + f'?per_page=100&page={page}'))['jobs']
        jobs.extend(batch)
        if len(batch) < 100:
            break
        page += 1
    if job_name and not any(job['name'] == job_name for job in jobs):
        raise SystemExit('No job with that exact name in this run attempt: ' + job_name)
    output = ROOT / 'build' / 'runs' / run_id / f'attempt-{attempt}'
    output.mkdir(parents=True, exist_ok=True)
    path = output / ('report.json' if not job_name else f"report-job-{next(j['id'] for j in jobs if j['name'] == job_name)}.json")
    cached_jobs = {}
    try:
        previous = json.loads(path.read_text(encoding='utf-8'))
        if (previous['runId'], previous['attempt'], previous['commit']) == (run_id, attempt, run['head_sha']):
            cached_jobs = {entry['id']: entry for entry in previous['jobs']}
    except (OSError, ValueError, KeyError, TypeError):
        pass
    result = {'runId': run_id, 'attempt': attempt, 'commit': run['head_sha'],
              'url': run['html_url'], 'status': run['status'],
              'conclusion': run['conclusion'], 'jobs': []}
    print(f"Run {run_id}, attempt {attempt}, source {run['head_sha']}")
    print(f"Actions: {run['status']} / {run['conclusion']}; {run['html_url']}")
    if run['status'] == 'completed':
        seconds = (datetime.fromisoformat(run['updated_at']) -
                   datetime.fromisoformat(run['created_at'])).total_seconds()
        print(f"Dispatch-to-completion: {seconds:.0f}s (including runner queues)")
    for job in jobs:
        if job_name and job['name'] != job_name:
            continue
        item = {key: job[key] for key in ('id', 'name', 'status', 'conclusion', 'steps')}
        result['jobs'].append(item)
        print(f"\n{job['name']}: {job['status']} / {job['conclusion']}")
        if job.get('started_at'):
            offset=(datetime.fromisoformat(job['started_at'])-datetime.fromisoformat(run['created_at'])).total_seconds()
            print(f'  Start offset: {offset:.0f}s from dispatch (includes dependency/runner waits)')
        current=next((step['name'] for step in job['steps'] if step['status']=='in_progress'),None)
        if current:print('  Running: '+current)
        durations = [(step['name'], (datetime.fromisoformat(step['completed_at']) -
                                    datetime.fromisoformat(step['started_at'])).total_seconds())
                     for step in job['steps'] if step.get('completed_at') and step.get('started_at')]
        item['stepDurationsSeconds'] = dict(durations)
        for name, seconds in sorted(durations, key=lambda entry: entry[1], reverse=True)[:3]:
            print(f"  Time: {seconds:.0f}s {name}")
        for step in job['steps']:
            if step['conclusion'] not in (None, 'success', 'skipped'):
                print(f"  Step {step['number']}: {step['name']} -> {step['conclusion']}")
        if job['status'] != 'completed' or job['conclusion'] == 'skipped':
            item['logState'] = 'not fetched: job running or skipped'
            continue
        log_path = output / f"job-{job['id']}.log"
        log_format = 'actions-job-api'
        data = None
        cached = cached_jobs.get(job['id'], {})
        if cached.get('status') == 'completed' and cached.get('conclusion') == job['conclusion']:
            try:
                candidate = log_path.read_bytes()
                if (hashlib.sha256(candidate).hexdigest() == cached.get('logSHA256') and
                        cached.get('logFormat') in ('actions-job-api', 'gh-run-view')):
                    data = candidate
                    log_format = cached['logFormat']
            except OSError:
                pass
        try:
            if data is None:
                data = api(f"actions/jobs/{job['id']}/logs")
        except SystemExit:
            # The individual-job endpoint can fail while the run ZIP remains
            # readable. Let gh use its run-log association for this exact job
            # and attempt; never silently substitute another execution.
            fallback = subprocess.run(['gh', 'run', 'view', run_id, '--repo', REPO,
                                       '--attempt', str(attempt), '--job', str(job['id']), '--log'],
                                      capture_output=True, timeout=60)
            if fallback.returncode or not fallback.stdout:
                item['logState'] = 'unavailable (not evidence of success)'
                print('  ' + item['logState'])
                continue
            data = fallback.stdout
            log_format = 'gh-run-view'
        log_path.write_bytes(data)
        item['logState'] = 'downloaded'
        item['logFile'] = log_path.name
        item['logSHA256'] = hashlib.sha256(data).hexdigest()
        item['logFormat'] = log_format
        # Match only actual output at line start after the Actions timestamp,
        # never echoed shell/Python source containing the same marker.
        lines = data.decode('utf-8', errors='replace').splitlines()
        if log_format == 'gh-run-view':
            lines = [re.sub(r'^[^\t]+\t[^\t]+\t', '', line) for line in lines]
        lines = [re.sub(r'^\d{4}-\d\d-\d\dT\S+\s+', '', line) for line in lines]
        markers=('APP_BUILD ','APP_CASE ','APP_DIAGNOSIS ','APP_SUITE ','APP_INPUT_VERIFIED ','APP_INPUT_HANDOFF ',
                 'COMPILER_CACHE_STATS ','EXPERIMENT_RECEIPT ')
        item['observations']=[line for line in lines if line.startswith(markers)]
        for line in (lines if verbose else item['observations']):
            print('  '+line)
        # An uncaught failure can have no application markers (e.g. compiler
        # error). Preserve actionable Actions errors instead of a blank report.
        item['errors'] = [line for line in lines if line.startswith('##[error]') or
                          re.search(r':\d+(?::\d+)?: (?:fatal )?error:', line)]
        for line in item['errors'][-8:]:
            print('  ' + line)
        print('  Raw log: ' + str(log_path))
    path.write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    print('\nReport: ' + str(path))
    print('LLDB command success describes evidence collection only. App status 0 alone does not certify a GUI journey.')
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('command', choices=['build', 'status', 'report', 'download'])
    parser.add_argument('--app',default='all',help='all or comma-separated source-owned app IDs; subset runs are explicitly partial')
    parser.add_argument('--reference',action='store_true',help='Also run the same app journeys on the Apple-host reference')
    parser.add_argument('--lifecycle-context',action='store_true',help='Opt-in read-only loaded lifecycle ABI and host window-state evidence on Linux')
    parser.add_argument('--run-id', help='Exact GitHub Actions run to inspect or download')
    parser.add_argument('--ref', default='main', help='Pushed revision to build')
    parser.add_argument('--job', help='Exact job name for report; default: all jobs')
    parser.add_argument('--verbose', action='store_true', help='Print all diagnostic observations in report')
    parser.add_argument('--evidence',action='store_true',help='Report and download/hash-verify the exact completed execution evidence')
    parser.add_argument('--wait',action='store_true',help='Follow the exact newly dispatched run, then report its result without redispatch')
    parser.add_argument('--artifact', help='Download only this named artifact from the exact run')
    args = parser.parse_args()
    if args.command in ('report', 'download') and (not args.run_id or not args.run_id.isdecimal()):
        parser.error(args.command + ' requires --run-id NUMERIC_RUN_ID')
    if args.job and args.command != 'report':
        parser.error('--job is only supported by report')
    if args.artifact and args.command != 'download':
        parser.error('--artifact is only supported by download')
    if args.wait and args.command!='build':parser.error('--wait is only supported by build')
    if args.evidence and not (args.command=='report' or (args.command=='build' and args.wait)):
        parser.error('--evidence requires report or build --wait')
    if args.command == 'report':
        result=report(args.run_id, args.job, args.verbose)
        if args.evidence:collect_evidence(result)
        return
    repo = REPO
    if args.command == 'build':
        # A failed push must not silently dispatch stale remote sources. Resolve
        # the explicitly selected local ref, including when invoked elsewhere.
        root = Path(__file__).resolve().parents[1]
        expected = subprocess.check_output(
            ['git', '-C', str(root), 'rev-parse', '--verify', args.ref + '^{commit}'],
            text=True).strip()
        remote = subprocess.check_output(
            ['gh', 'api', 'repos/' + repo + '/commits/' + args.ref, '--jq', '.sha'],
            text=True).strip()
        if expected != remote:
            raise SystemExit('Selected local ref is not pushed: ' + args.ref +
                             ' local=' + expected + ' remote=' + remote)
        print('Dispatching exact pushed source ' + expected, flush=True)
        command = ['workflow', 'run', 'experiment.yml', '--ref', args.ref, '-f', 'app=' + args.app, '-f', 'reference=' + str(args.reference).lower(),
                   '-f','lifecycle_context='+str(args.lifecycle_context).lower()]
        dispatched=subprocess.run(['gh',*command,'--repo',repo],capture_output=True,text=True,check=True)
        dispatch_output=dispatched.stdout+dispatched.stderr
        print(dispatch_output, end='',flush=True)
        if args.wait:
            match=re.search(r'https://github\.com/'+re.escape(repo)+r'/actions/runs/(\d+)',dispatch_output)
            if not match:raise SystemExit('Dispatch accepted but exact run URL was not returned; inspect status, do not redispatch')
            run_id=match[1]
            wait_for_run(run_id)
            result=report(run_id,None,args.verbose)
            if args.evidence:collect_evidence(result)
            if result['conclusion']!='success':raise SystemExit(1)
        return
    elif args.command == 'status':
        command = (['run', 'view', args.run_id] if args.run_id else
                   ['run', 'list', '--workflow', 'experiment.yml', '--limit', '5'])
    else:
        if not args.run_id or not args.run_id.isdecimal():
            parser.error('download requires --run-id NUMERIC_RUN_ID')
        output = Path(__file__).resolve().parents[1] / 'build' / 'runs' / args.run_id
        if args.artifact:
            if Path(args.artifact).name != args.artifact or args.artifact in ('.', '..'):
                parser.error('artifact must be a single GitHub artifact name')
            output = output / args.artifact
        command = ['run', 'download', args.run_id, '--dir', str(output)]
        if args.artifact:
            command += ['--name', args.artifact]
    subprocess.run(['gh', *command, '-R', repo], check=True)


if __name__ == '__main__':
    main()
