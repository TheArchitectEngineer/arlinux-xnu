#!/usr/bin/env python3
"""Record hosted experiment provenance, including failed execution artifacts."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import subprocess
from datetime import datetime, timezone

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--directory', required=True)
parser.add_argument('--outcome', required=True)
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]
output = root / args.directory
output.mkdir(parents=True, exist_ok=True)
files = []
for path in sorted(output.rglob('*')):
    if path.is_file() and not path.is_symlink() and path.name != 'receipt.json':
        digest = hashlib.sha256()
        with path.open('rb') as source:
            for block in iter(lambda: source.read(1024 * 1024), b''):
                digest.update(block)
        files.append({'path': path.relative_to(output).as_posix(),
                      'bytes': path.stat().st_size, 'sha256': digest.hexdigest()})
receipt = {
    'schemaVersion': 1,
    'recordedAt': datetime.now(timezone.utc).isoformat(),
    'repository': os.environ['GITHUB_REPOSITORY'],
    'commit': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=root, text=True).strip(),
    'runId': os.environ['GITHUB_RUN_ID'],
    'runAttempt': os.environ['GITHUB_RUN_ATTEMPT'],
    'job': os.environ['GITHUB_JOB'],
    'executionOutcomeBeforeUpload': args.outcome,
    'runner': {'os': platform.platform(), 'architecture': platform.machine(),
               'image': os.environ.get('ImageOS'), 'imageVersion': os.environ.get('ImageVersion')},
    'sourceLock': json.loads((root / 'sources.lock.json').read_text()),
    'files': files,
}
receipt_path = output / 'receipt.json'
receipt_path.write_text(json.dumps(receipt, indent=2) + '\n')
step_output=os.environ.get('GITHUB_OUTPUT')
if step_output:
    with open(step_output,'a') as stream:
        stream.write('sha256='+hashlib.sha256(receipt_path.read_bytes()).hexdigest()+'\n')
# Expose provenance and executable hashes in Actions logs. Diagnosing a run
# must not require downloading its kernel objects or complete artifact archive.
print('EXPERIMENT_RECEIPT ' + json.dumps({
    'job': receipt['job'], 'directory': args.directory, 'commit': receipt['commit'], 'runId': receipt['runId'],
    'outcomeBeforeUpload': args.outcome, 'filesRecorded': len(files),
    'receiptSHA256': hashlib.sha256(receipt_path.read_bytes()).hexdigest(),
}))
for item in files:
    if os.access(output / item['path'], os.X_OK):
        print('EXPERIMENT_EXECUTABLE ' + json.dumps(item))
summary = os.environ.get('GITHUB_STEP_SUMMARY')
if summary:
    with open(summary, 'a') as stream:
        stream.write(f"### {receipt['job']} / {args.directory}\n\nExecution before upload: **{args.outcome}**. "
                     f"Commit: `{receipt['commit']}`. Files recorded: {len(files)}.\n")
