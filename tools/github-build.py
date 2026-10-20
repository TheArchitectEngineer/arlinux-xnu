#!/usr/bin/env python3
"""Dispatch and inspect the hosted XNU experiment; never compile locally."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
from datetime import datetime


REPO = 'taowen/arlinux-xnu'
ROOT = Path(__file__).resolve().parents[1]


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
        markers = ('APP_INPUT ', 'APP_OUTCOME ', 'COMPILER_CACHE_STATS ', 'LLDB_RETRY_SKIPPED ', 'APP_PROCESS_STATUS=', 'LLDB_COMMAND_STATUS=',
                   'DARLING_NATIVE_CONNECTED ', 'DARLING_RPC_ERROR ', 'DARLING_RPC_WAIT ',
                   'DARLING_RENDER_RECEIVED ', 'DARLING_PORT_MOVE_MEMBER ', 'NATIVE_SERVICE_PORTS ',
                   'HIDFOCUS_REQUEST_UNIMPLEMENTED ', 'SESSION_SERVICE_FAILED ',
                   'WAYLAND_TOPLEVEL_FOCUS ',
                   'UI_RUNLOOP_OBSERVER_', 'UI_LAYER_FRAME ', 'UI_BITMAP_CONTEXT_UNAVAILABLE ',
                   'UI_FRAME_CAPTURE', 'WAYLAND_APP_FRAME_', 'APP_FRAME_CAPTURE ',
                   'DARLINGSERVER_RPC_ONLY ',
                   'DARLING_KQCHAN_OPEN ', 'DARLING_KQCHAN_EVENT ',
                   'DARLING_SERVICE_REPLY ',
                   'DARLING_SERVER_UNAVAILABLE ', 'darlingserver duct-tape panic: ',
                   'DARLING_S2C_MEMORY ', 'DARLING_PSYNCH ', 'NATIVE_THREAD_IDENTITIES ', 'NATIVE_ULOCK_WAIT ', 'NATIVE_ULOCK_QUEUES ', 'DARLING_CODE_COPY_COMPLETE ',
                   'SESSION_MACH_CORE_STARTED ', 'SESSION_MACH_CORE_FAILED ', 'UNIFIED_SESSION_PID ',
                   'DARWIN_WORKLOOP_SOFT_DELETE ', 'DARWIN_WORKLOOP_ERROR_STATE ',
                   'DARWIN_WORKLOOP_SYNC_', 'NATIVE_WORKLOOP_WAIT ',
                   'DARWIN_CODESIGN_QUERY ', 'SECURITY_QUERY ', 'SECURITY_RETURN ',
                   'EXCEPTION_CALLER_CODE ', 'EXCEPTION_CALLER_REFERENCE ',
                   'DICTIONARY_NIL_INPUT ', 'DICTIONARY_INPUT_READ_ERROR ', 'HID_CLIENT_REFERENCE ',
                   'HID_CLIENT_CALLEE_REFERENCE ',
                   'IPC_CALLBACK ',
                   'DARLING_PORT_CONSTRUCT ', 'DARLING_UNSUPPORTED_SERVICE ', 'DARLING_SERVICE_DESCRIPTOR ',
                   'DARLING_SERVICE_INLINE ', 'NATIVE_SERVICE_FAILURE ', 'NATIVE_RENDER_FRAMEWORK_', 'NATIVE_RENDER_FORWARD ', 'ORIGINAL_WAIT_PID=', 'ORIGINAL_WAIT_START_TICKS ', 'ORIGINAL_WAIT_TRACE_STATUS=',
                   'DARLING_PORT_GUARD ',
                   'APP_STARTUP_INCOMPLETE ', 'DARWIN_EXIT_FRAME ',
                   'GUEST_EXIT_STATUS ', 'OBJC_EXCEPTION ', 'OBJC_EXCEPTION_DECODE_ERROR ',
                   'SERVICE_LOOKUP ', 'SERVICE_LOOKUP_LIMIT ',
                   'WAYLAND_BACKEND_READY ', 'NATIVE_SERVICE_ADAPTER ', 'DARWIN_DISPLAY_QUERY ',
                   'DARWIN_RENDER_DISPLAY_LIST ', 'OBJC_DISPLAY_DECODE_ERROR ',
                   'DARWIN_RENDER_PROPERTIES ', 'DISPLAY_MODE_RATE_FACTOR ',
                   'DARWIN_TIMER_OPERATION ', 'DARWIN_TIMER_EXPIRED ', 'DARWIN_PORT_OPERATION ',
                   'DARWIN_PORT_TYPE ', 'SERVICE_REGISTER ', 'GRAPHICS_FAILURE ', 'DARWIN_SERVICE_REGISTER ',
                   'DARWIN_KEVENT_BUFFER_DELIVERY ',
                   'DARWIN_MACH_OPTIONS_REJECTED ',
                   'DARWIN_REPLY_PORT_DESTROYED ',
                   'DARWIN_REPLY_RECEIVE_DIED ',
                   'DARWIN_MACH_SEND_DATA_REJECTED ', 'FAULT_REFERENCE ',
                   'IPC_WAIT ', 'IPC_WAIT_DECODE_ERROR ', 'IPC_QUEUE_SNAPSHOT ', 'DARWIN_KEVENT_ERROR ',
                   'NORMAL_RUNTIME_FAULT_PC ', 'DIAGNOSTIC_WAIT_INTERRUPTED ',
                   'DARWIN_EVENT_DELIVERY ', 'DARWIN_NATIVE_FAULT ', 'DYNAMIC_MAIN ', 'DYNAMIC_THREAD ',
                   'DYNAMIC_CONSOLE ', 'EXPERIMENT_RECEIPT ', 'EXPERIMENT_EXECUTABLE ',
                   'BOOTSTRAP_XPC_REQUEST ', 'BOOTSTRAP_XPC_REPLY ', 'BOOTSTRAP_XPC_IDENTITY ',
                   'PROCESSINFO_', 'SESSION_XPC_', 'SESSION_WORKSPACE_', 'SESSION_LAUNCH_', 'SESSION_SCENE_', 'SESSION_EVENT_', 'SESSION_READY ', 'PEER_PROCESS_STATUS=',
                   'APP_LAUNCH_CALL ', 'APP_LAUNCH_METADATA_ERROR ', 'APP_SETTINGS_CALL ')
        item['observations'] = []
        context_lengths = {'DARLING_NATIVE_CONNECTED ': 2,
                           'DARLING_KQCHAN_OPEN ': 3, 'DARLING_KQCHAN_EVENT ': 5,
                           'DARLING_SERVICE_REPLY ': 4,
                           'DARLING_PORT_CONSTRUCT ': 5,
                           'DARLING_PORT_GUARD ': 5,
                           'DARLING_S2C_MEMORY ': 6, 'DARLING_PSYNCH ': 10, 'DARLING_UNSUPPORTED_SERVICE ': 6, 'DARLING_SERVICE_DESCRIPTOR ': 5,
                           'DARWIN_WORKLOOP_SOFT_DELETE ': 2,
                           'DARWIN_WORKLOOP_ERROR_STATE ': 9,
                           'DARWIN_CODESIGN_QUERY ': 6,
                           'DARWIN_EVENT_DELIVERY ': 3, 'DARWIN_NATIVE_FAULT ': 3,
                           'DARWIN_DISPLAY_QUERY ': 3, 'DARWIN_RENDER_DISPLAY_LIST ': 3,
                           'DARWIN_RENDER_PROPERTIES ': 4, 'DARWIN_TIMER_OPERATION ': 6,
                           'DARWIN_TIMER_EXPIRED ': 1, 'DARWIN_PORT_OPERATION ': 6,
                           'DARWIN_KEVENT_ERROR ': 6, 'DARWIN_PORT_TYPE ': 3,
                           'DARWIN_KEVENT_BUFFER_DELIVERY ': 4,
                           'DARWIN_MACH_OPTIONS_REJECTED ': 7, 'DARWIN_SERVICE_REGISTER ': 3,
                           'DARWIN_REPLY_PORT_DESTROYED ': 2, 'DARWIN_REPLY_RECEIVE_DIED ': 1,
                           'DARWIN_MACH_SEND_DATA_REJECTED ': 7}
        for index, line in enumerate(lines):
            if not line.startswith(markers):
                continue
            item['observations'].append(line)
            for prefix, count in context_lengths.items():
                if not line.startswith(prefix):
                    continue
                # Preserve bounded context verbatim: separate worker writes may
                # interleave, so do not infer a synthetic causal event from it.
                item['observations'].extend('  context: ' + text for text in lines[index+1:index+1+count])
                break
        # Keep full observations in the JSON and raw log. Default output must
        # expose outcomes before the many historical instrumentation markers.
        if verbose:
            for line in item['observations']:
                print('  ' + line)
        else:
            concise = ('APP_OUTCOME ', 'COMPILER_CACHE_STATS ', 'APP_PROCESS_STATUS=', 'LLDB_COMMAND_STATUS=',
                       'LLDB_RETRY_SKIPPED ', 'APP_STARTUP_INCOMPLETE ',
                       'ORIGINAL_WAIT_PID=', 'ORIGINAL_WAIT_START_TICKS ', 'ORIGINAL_WAIT_TRACE_STATUS=',
                       'NATIVE_SERVICE_PORTS ', 'NATIVE_ULOCK_QUEUES ', 'NATIVE_ULOCK_WAIT ',
                       'NORMAL_RUNTIME_FAULT_PC ', 'DARWIN_NATIVE_FAULT ', 'DARWIN_KEVENT_ERROR ',
                       'OBJC_EXCEPTION ', 'OBJC_EXCEPTION_DECODE_ERROR ',
                       'NATIVE_SERVICE_FAILURE ', 'DARLING_RENDER_RECEIVED ',
                       'HIDFOCUS_REQUEST_UNIMPLEMENTED ', 'SESSION_SERVICE_FAILED ',
                       'WAYLAND_TOPLEVEL_FOCUS ',
                       'UI_RUNLOOP_OBSERVER_', 'UI_LAYER_FRAME ', 'UI_BITMAP_CONTEXT_UNAVAILABLE ',
                       'UI_FRAME_CAPTURE', 'WAYLAND_APP_FRAME_', 'APP_FRAME_CAPTURE ',
                       'DARLING_PORT_MOVE_MEMBER ', 'NATIVE_RENDER_FORWARD ',
                       'DARWIN_WORKLOOP_ERROR_STATE ', 'DARWIN_WORKLOOP_SYNC_', 'NATIVE_WORKLOOP_WAIT ',
                       'EXPERIMENT_RECEIPT ')
            shown = set()
            ordered = sorted(enumerate(item['observations']),
                             key=lambda entry: 0 if entry[1].startswith('APP_OUTCOME ') else
                             2 if entry[1].startswith('EXPERIMENT_RECEIPT ') else 1)
            for index, line in ordered:
                if line.startswith(concise) and line not in shown:
                    print('  ' + line)
                    shown.add(line)
                    if line.startswith(('DARWIN_WORKLOOP_ERROR_STATE ', 'DARWIN_NATIVE_FAULT ', 'DARWIN_KEVENT_ERROR ')):
                        for context in item['observations'][index+1:index+10]:
                            if not context.startswith('  context: '):
                                break
                            print('  ' + context)
            print(f"  Full observations: {len(item['observations'])}; use --verbose or the saved JSON/raw log")
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


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('command', choices=['build', 'status', 'report', 'download'])
    parser.add_argument('--mode', choices=['both', 'host', 'baseline', 'native', 'dyld', 'app', 'playcover', 'darling'], default='native')
    parser.add_argument('--ipc', choices=['local', 'darling'], default='local', help='IPC backend for the native dyld/app journey')
    parser.add_argument('--run-id', help='Exact GitHub Actions run to inspect or download')
    parser.add_argument('--ref', default='main', help='Pushed revision to build')
    parser.add_argument('--job', help='Exact job name for report; default: all jobs')
    parser.add_argument('--verbose', action='store_true', help='Print all diagnostic observations in report')
    parser.add_argument('--artifact', help='Download only this named artifact from the exact run')
    args = parser.parse_args()
    if args.command in ('report', 'download') and (not args.run_id or not args.run_id.isdecimal()):
        parser.error(args.command + ' requires --run-id NUMERIC_RUN_ID')
    if args.job and args.command != 'report':
        parser.error('--job is only supported by report')
    if args.artifact and args.command != 'download':
        parser.error('--artifact is only supported by download')
    if args.command == 'report':
        report(args.run_id, args.job, args.verbose)
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
        command = ['workflow', 'run', 'experiment.yml', '--ref', args.ref, '-f', 'mode=' + args.mode, '-f', 'ipc=' + args.ipc]
    elif args.command == 'status':
        command = (['run', 'view', args.run_id] if args.run_id else
                   ['run', 'list', '--workflow', 'experiment.yml', '--limit', '5'])
    else:
        if not args.run_id or not args.run_id.isdecimal():
            parser.error('download requires --run-id NUMERIC_RUN_ID')
        output = Path(__file__).resolve().parents[1] / 'build' / 'runs' / args.run_id
        command = ['run', 'download', args.run_id, '--dir', str(output)]
        if args.artifact:
            command += ['--name', args.artifact]
    subprocess.run(['gh', *command, '-R', repo], check=True)


if __name__ == '__main__':
    main()
