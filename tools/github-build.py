#!/usr/bin/env python3
"""Dispatch and inspect the hosted XNU experiment; never compile locally."""
import argparse
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('command', choices=['build', 'status', 'download'])
    parser.add_argument('--mode', choices=['both', 'host', 'baseline', 'native', 'dyld', 'app'], default='native')
    parser.add_argument('--run-id', help='Exact GitHub Actions run to inspect or download')
    parser.add_argument('--ref', default='main', help='Pushed revision to build')
    args = parser.parse_args()
    repo = 'taowen/arlinux-xnu'
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
        command = ['workflow', 'run', 'experiment.yml', '--ref', args.ref, '-f', 'mode=' + args.mode]
    elif args.command == 'status':
        command = (['run', 'view', args.run_id] if args.run_id else
                   ['run', 'list', '--workflow', 'experiment.yml', '--limit', '5'])
    else:
        if not args.run_id or not args.run_id.isdecimal():
            parser.error('download requires --run-id NUMERIC_RUN_ID')
        output = Path(__file__).resolve().parents[1] / 'build' / 'runs' / args.run_id
        command = ['run', 'download', args.run_id, '--dir', str(output)]
    subprocess.run(['gh', *command, '-R', repo], check=True)


if __name__ == '__main__':
    main()
