#!/usr/bin/env python3
"""Dispatch and inspect the hosted XNU experiment; never compile locally."""
import argparse
from contextlib import redirect_stdout
import hashlib
import io
import json
from pathlib import Path
import re
import subprocess
import time
from datetime import datetime
from app_suite.selection import select


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
    verified=[]
    for job in run['jobs']:
        artifact={'native-userland':'ios-app-results-linux','apple-reference':'ios-app-results-apple'}.get(job['name'])
        if not artifact or (job['status']!='completed' and not job.get('stepsFinished')) or job['conclusion']=='skipped':continue
        receipts=[json.loads(line[len('EXPERIMENT_RECEIPT '):]) for line in job.get('observations',[])
                  if line.startswith('EXPERIMENT_RECEIPT ')]
        matches=[receipt for receipt in receipts if receipt['directory']=='build/results'
                 and receipt['commit']==run['commit'] and str(receipt['runId'])==str(run['runId'])]
        if len(matches)!=1:
            print(f'  {artifact}: producing receipt unavailable; no evidence verified')
            continue
        directory=ROOT/'build/runs'/str(run['runId'])/f"attempt-{run['attempt']}"/artifact
        expected=matches[0]['receiptSHA256']
        if directory.is_symlink() or not directory.parent.resolve().is_relative_to(ROOT.resolve()):
            raise SystemExit('Evidence cache is outside the workspace or is a symlink')
        count=None
        if directory.exists():
            try:count=verify_result_files(directory,expected,run)
            except (OSError,ValueError,KeyError) as error:
                rejected=directory.with_name(artifact+'.unverified-'+str(time.time_ns()))
                directory.rename(rejected)
                print(f'  Rejected cached evidence: {error}; preserved at {rejected}')
        if count is None:
            directory.parent.mkdir(parents=True,exist_ok=True)
            staging=directory.with_name(artifact+'.partial-'+str(time.time_ns()))
            staging.mkdir()
            subprocess.run(['gh','run','download',str(run['runId']),'--repo',REPO,
                '--name',artifact,'--dir',str(staging)],check=True,timeout=120)
            try:count=verify_result_files(staging,expected,run)
            except (OSError,ValueError,KeyError) as error:
                raise SystemExit(f'Fresh evidence verification failed: {error}; retained at {staging}') from error
            try:staging.rename(directory)  # Only complete, hash-verified downloads become reusable.
            except OSError:
                # Another observer may have published this exact run while
                # our download was in flight. Accept only a verified winner.
                if not directory.is_dir():raise
                count=verify_result_files(directory,expected,run)
                print(f'  Concurrent evidence verified; duplicate download retained at {staging}')
        print(f'  Verified {artifact}: receipt plus {count} files; {directory}')
        result_path=directory/'apps/result.json'
        covered={entry['path'] for entry in json.loads((directory/'receipt.json').read_bytes())['files']}
        if 'apps/result.json' in covered:
            suite=json.loads(result_path.read_bytes());cases=suite['cases']
            passed=sum(case['status']=='passed' for case in cases)
            journey=sum(case.get('journeySeconds',0) for case in cases)
            diagnosis=sum(step.get('diagnosticSeconds',0) for case in cases for step in case['steps'])
            scope='full collection' if suite['fullSuite'] else 'partial selection'
            print(f'  Verified effects: {passed}/{len(cases)} passed ({scope}); journey={journey:.1f}s, diagnosis={diagnosis:.1f}s included')
            for case in sorted(cases,key=lambda case:case.get('journeySeconds',0),reverse=True)[:3]:
                failed=next((step for step in case['steps'] if step['status']=='failed'),{})
                detail=(f"; failed {failed['do']}, effect={failed.get('seconds')}s, diagnosis={failed.get('diagnosticSeconds')}s" if failed else '')
                print(f"    Slow journey: {case['id']} {case.get('journeySeconds')}s {case['status']}{detail}")
        verified.append(directory)
    return verified


def compare_runs(current, baseline):
    """Compare retained app effects only after checking both producing receipts."""
    if current['status']!='completed':raise SystemExit('Comparison requires a completed current run')
    # Keep the complete baseline report on disk without printing its entire
    # historical diagnosis again beside the current failure.
    with redirect_stdout(io.StringIO()):before=report(baseline,None)
    print(f"Baseline run {baseline}, source {before['commit']}: {before['url']}")
    if before['status']!='completed':raise SystemExit('Comparison requires a completed baseline run')
    old_dirs=collect_evidence(before);new_dirs=collect_evidence(current)
    comparisons=[]
    for directory in new_dirs:
        old=next((path for path in old_dirs if path.name==directory.name),None)
        if old is None:continue
        old_path=old/'apps/result.json';new_path=directory/'apps/result.json'
        if not old_path.is_file() or not new_path.is_file():continue
        for parent in (old,directory):
            receipt=json.loads((parent/'receipt.json').read_text())
            if 'apps/result.json' not in {entry['path'] for entry in receipt['files']}:
                raise SystemExit('App result is not covered by the verified receipt')
        left=json.loads(old_path.read_text());right=json.loads(new_path.read_text())
        previous={case['id']:case for case in left['cases']}
        following={case['id']:case for case in right['cases']}
        for name in sorted(set(previous)|set(following)):
            a=previous.get(name);b=following.get(name)
            row={'id':name,'backend':right['backend'],'before':a and a['status'],'after':b and b['status']}
            if not a or not b:
                row['comparison']='not run in both selections'
            else:
                owner,journey=name.split('--',1)
                path=f'apps/{owner}/journeys.json' if owner!='session' else f'apps/scenarios/{journey}.json'
                matched=(a['journeySHA256']==b['journeySHA256'] if a.get('journeySHA256') and b.get('journeySHA256')
                         else path in left['inputs'] and left['inputs'][path]==right['inputs'].get(path))
                row['sameJourney']=matched
                if not matched:row['comparison']='journey changed; no progress verdict'
                elif a['status']!=b['status']:row['comparison']='now passes; cause not established' if b['status']=='passed' else 'regression'
                else:
                    ai=next((i for i,s in enumerate(a['steps']) if s['status']=='failed'),None)
                    bi=next((i for i,s in enumerate(b['steps']) if s['status']=='failed'),None)
                    row.update(beforeFailedStep=ai,afterFailedStep=bi)
                    row['comparison']=('later failed step' if bi>ai else 'earlier failed step' if bi<ai else 'same failed step') if ai is not None and bi is not None else 'same status'
                for label,case in (('before',a),('after',b)):
                    row[label+'Seconds']=case.get('journeySeconds')
                    row[label+'DiagnosticSeconds']=round(sum(step.get('diagnosticSeconds',0) for step in case['steps']),3)
                    failed=next((step for step in case['steps'] if step['status']=='failed'),{})
                    row[label+'Failure']={key:failed[key] for key in ('do','text','reason') if key in failed}
            comparisons.append(row)
            if a and b:
                print(f"  COMPARE {name}: {a['status']} -> {b['status']}; {row['comparison']}")
                print(f"    journey={row.get('beforeSeconds')}s -> {row.get('afterSeconds')}s; diagnosis={row.get('beforeDiagnosticSeconds')}s -> {row.get('afterDiagnosticSeconds')}s")
                if row.get('afterFailure'):print('    Current failed effect: '+json.dumps(row['afterFailure']))
    omitted=[row['id'] for row in comparisons if row['before'] is None or row['after'] is None]
    if omitted:print('  Not run in both selections (no verdict): '+', '.join(omitted))
    path=ROOT/'build/runs'/str(current['runId'])/f"attempt-{current['attempt']}"/('comparison-'+baseline+'.json')
    path.write_text(json.dumps({'baseline':baseline,'current':current['runId'],'cases':comparisons},indent=2)+'\n')
    print('Comparison: '+str(path))
    if not comparisons:print('No matching verified app result artifacts; comparison unavailable')


def wait_for_run(run_id,commit):
    previous=None
    job_states={};reported_failures=set()
    while True:
        try:run=json.loads(api('actions/runs/'+run_id))
        except SystemExit:
            print(f'Observation unavailable; keep following run {run_id}, no redispatch',flush=True)
            time.sleep(15)
            continue
        if run['head_sha']!=commit:raise SystemExit('Dispatched source differs from the selected pushed commit')
        status=(run['status'],run['conclusion'])
        if status!=previous:
            print(f'Run {run_id}: {status[0]} / {status[1]}',flush=True);previous=status
        if run['status']=='completed':return
        try:
            jobs=json.loads(api(f"actions/runs/{run_id}/attempts/{run['run_attempt']}/jobs?per_page=100"))['jobs']
        except SystemExit:
            jobs=[]  # Keep the known run, not a replacement dispatch.
        for job in jobs:
            current=next((step['name'] for step in job['steps'] if step['status']=='in_progress'),None)
            state=(job['status'],job['conclusion'],current)
            if state!=job_states.get(job['id']):
                print(f"  {job['name']}: {state[0]} / {state[1]}"+(f' / {current}' if current else ''),flush=True)
                job_states[job['id']]=state
            if job['conclusion']=='failure' and job['id'] not in reported_failures:
                report(run_id,job['name']);reported_failures.add(job['id'])
        time.sleep(10)


def api(endpoint):
    try:
        result = subprocess.run(['gh', 'api', '--allow-escape-sequences', 'repos/' + REPO + '/' + endpoint],
                                capture_output=True, timeout=60)
    except (subprocess.TimeoutExpired,OSError) as error:
        raise SystemExit('GitHub API observation unavailable: '+endpoint) from error
    if result.returncode:
        # Redirect failures can include signed download URLs in stderr.
        raise SystemExit('GitHub API request failed: ' + endpoint)
    return result.stdout


def print_observation(line):
    """Bound terminal output only; report.json retains the original observation."""
    if line.startswith('APP_ORIGINAL_INPUT '):
        try:
            entry=json.loads(line.split(' ',1)[1])
            if 'originalInput' in entry:
                origin=entry['originalInput']
                entry={key:entry[key] for key in ('id','version','build','error') if key in entry}
                entry.update({key:origin[key] for key in ('bundleIdentifier','binarySHA256',
                    'bundleManifestSHA256','downloadURL','downloadSeconds') if key in origin})
                entry['evidence']='input-'+entry['id']+'.json'
            print('  APP_ORIGINAL_INPUT '+json.dumps(entry))
        except (ValueError,KeyError):print('  '+line[:512])
        return
    if not line.startswith('APP_DIAGNOSIS '):
        print('  '+line);return
    try:entry=json.loads(line[len('APP_DIAGNOSIS '):])
    except ValueError:
        print('  '+line);return
    failed=entry.get('failedStep',{})
    print(f"  APP_DIAGNOSIS {entry.get('id')}: {failed.get('do')} {failed.get('reason')}")
    print(f"    effect={failed.get('seconds')}s diagnosis={failed.get('diagnosticSeconds')}s blocked={entry.get('blockedSteps')} infrastructureFailure={entry.get('infrastructureFailure')}")
    if failed.get('observedApplicationState'):print('    Actual application state: '+json.dumps(failed['observedApplicationState']))
    for process in entry.get('processes',[]):
        print(f"    Process {process.get('app')} generation={process.get('generation')} identity={json.dumps(process.get('identity'))}")
        print(f"    Logs: {process.get('runtimeLog')}; {process.get('stderr')}")
        trace=process.get('startupTrace')
        if trace:
            print('    Original startup trace: '+json.dumps(trace))
        for fault in process.get('faultContexts',[]):
            print('    Fault: '+json.dumps(fault))
        for observed in process.get('observed',[]):
            if re.search(r'FAILED|FAULT|EXCEPTION|UNSUPPORTED|Assertion failed:',observed):
                print('    Observed: '+observed)
        missing=process.get('unimplementedSyscalls',{})
        for call in missing.get('calls',[]):
            callers=[frame.get('nearestSymbol',frame.get('pc')) for frame in call.get('originalCallers',[])[:2]]
            print(f"    ENOSYS nr={call.get('number')} count={call.get('count')} first callers={callers}")
        if missing.get('omittedSyscalls'):
            print(f"    Other ENOSYS syscall numbers: {missing['omittedSyscalls']} (full report retained)")
        for resource in missing.get('getattrlistPaths',[]):print('    getattrlist path: '+resource)
    screenshots=entry.get('screenshots',[])
    if screenshots:
        last=screenshots[-1]
        print(f"    Screenshot: {last.get('screenshot')} sha256={last.get('sha256')}")
        measured=next((shot for shot in reversed(screenshots) if 'pixels' in shot),None)
        if measured:print('    Last measured target: '+json.dumps({key:measured[key] for key in ('screenRect','pixels') if key in measured}))
    for observed in entry.get('sessionObserved',[]):print('    Session observed: '+observed)
    print('    Collection: '+json.dumps(entry.get('diagnostic')))
    print(f"    Full journey: {entry.get('fullJourney')}; full diagnosis remains in the saved report (--verbose prints raw observations)")


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
        result['dispatchToCompletionSeconds'] = seconds
        print(f"Dispatch-to-completion: {seconds:.0f}s (including runner queues)")
    # Parallel jobs shorten feedback but still consume separate runners. Keep
    # all jobs here even when --job filters the detailed diagnostic report.
    usage = []
    for job in jobs:
        if (job['conclusion'] == 'skipped' or not job.get('started_at') or
                not job.get('completed_at') or job['status'] != 'completed'):
            continue
        active = (datetime.fromisoformat(job['completed_at']) -
                  datetime.fromisoformat(job['started_at'])).total_seconds()
        usage.append({'job': job['name'], 'labels': job.get('labels', []),
                      'activeSeconds': active})
    result['runnerUsage'] = {'completedJobs': usage,
                            'activeSeconds': sum(job['activeSeconds'] for job in usage),
                            'complete': all(job['status'] == 'completed' for job in jobs)}
    print('Runner active time: ' + ', '.join(f"{job['job']}={job['activeSeconds']:.0f}s" for job in usage) +
          f"; total={result['runnerUsage']['activeSeconds']:.0f}s (completed jobs; not billed minutes)")
    for job in jobs:
        if job_name and job['name'] != job_name:
            continue
        item = {key: job[key] for key in ('id', 'name', 'status', 'conclusion', 'steps')}
        item['stepsFinished']=bool(job['steps']) and all(step['status']=='completed' for step in job['steps'])
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
        if (job['status'] != 'completed' and not item['stepsFinished']) or job['conclusion'] == 'skipped':
            item['logState'] = 'not fetched: steps running or job skipped'
            continue
        if job['status'] != 'completed':
            print('  All steps finished; reading recorded effects while GitHub job finalization remains pending')
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
        markers=('APP_BUILD ','APP_CASE ','APP_DIAGNOSIS ','APP_SUITE ','APP_INPUT_VERIFIED ','APP_INPUT_HANDOFF ','APP_IPA_DOWNLOAD ','APP_ORIGINAL_INPUT ',
                 'COMPILER_CACHE_STATS ','APPLE_DOWNLOAD_TIMING ','APPLE_PREPARATION_TIMING ','APPLE_VOLUME_INPUT ','EXPERIMENT_RECEIPT ')
        item['observations']=[line for line in lines if line.startswith(markers)]
        for line in (lines if verbose else item['observations']):
            if verbose:print('  '+line)
            else:print_observation(line)
        # An uncaught failure can have no application markers (e.g. compiler
        # error). Preserve actionable Actions errors instead of a blank report.
        item['errors'] = [line for line in lines if line.startswith('##[error]') or
                          re.search(r':\d+(?::\d+)?: (?:fatal )?error:', line) or
                          line.startswith(('clang: error:','ld: '))]
        for index,line in enumerate(lines):
            if line.startswith('Undefined symbols for architecture '):
                item['errors'].extend(lines[index:min(index+16,len(lines))])
        for line in dict.fromkeys(item['errors'][:24]+item['errors'][-8:]):
            print('  ' + line)
        print('  Raw log: ' + str(log_path))
    path.write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    print('\nReport: ' + str(path))
    print('LLDB command success describes evidence collection only. App status 0 alone does not certify a GUI journey.')
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('command', choices=['build', 'status', 'report', 'download', 'cases'])
    parser.add_argument('--app',help='Explicit all or comma-separated app IDs; build requires --app or --case')
    parser.add_argument('--case',help='Exact comma-separated journey IDs; only their app dependencies are compiled and run')
    parser.add_argument('--baseline-run',help='Compare receipt-verified effects against this exact completed run (report or build --wait)')
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
    if args.command=='build' and not (args.app or args.case):
        parser.error('Select --case JOURNEY_ID or --app APP_ID before dispatch; use --app all explicitly for full regression')
    if args.app is None:args.app='all'  # cases lists everything; --case resolves its own app dependencies.
    if args.baseline_run and (not args.baseline_run.isdecimal() or not (args.command=='report' or (args.command=='build' and args.wait))):
        parser.error('--baseline-run NUMERIC_ID requires report or build --wait')
    if args.case and args.command not in ('build','cases'):parser.error('--case requires build or cases')
    if args.command in ('build','cases'):
        try:products,cases,_,full_suite=select(ROOT,args.app,args.case)
        except ValueError as error:parser.error(str(error))
        print('Selection: '+str(len(cases))+' journeys; apps='+','.join(sorted(products))+'; fullSuite='+str(full_suite).lower(),flush=True)
        if args.command=='cases':
            for case in cases:print(case['id']+' ('+str(len(case['steps']))+' steps)')
            return
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
        if args.baseline_run:compare_runs(result,args.baseline_run)
        elif args.evidence:collect_evidence(result)
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
        if args.case:command+=['-f','case='+args.case]
        dispatched=subprocess.run(['gh',*command,'--repo',repo],capture_output=True,text=True)
        dispatch_output=dispatched.stdout+dispatched.stderr
        print(dispatch_output, end='',flush=True)
        if dispatched.returncode:
            raise SystemExit('Workflow dispatch request failed; inspect remote status before retrying')
        if args.wait:
            match=re.search(r'https://github\.com/'+re.escape(repo)+r'/actions/runs/(\d+)',dispatch_output)
            if not match:raise SystemExit('Dispatch accepted but exact run URL was not returned; inspect status, do not redispatch')
            run_id=match[1]
            wait_for_run(run_id,expected)
            result=report(run_id,None,args.verbose)
            if args.baseline_run:compare_runs(result,args.baseline_run)
            elif args.evidence:collect_evidence(result)
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
