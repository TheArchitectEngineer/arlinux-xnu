"""Verify actual downloaded products before execution, not a capability checklist."""
import hashlib,json,os
from pathlib import Path
root=Path(__file__).resolve().parents[1];directory=root/'build/apps'
receipt=(directory/'receipt.json').read_bytes()
if hashlib.sha256(receipt).hexdigest()!=os.environ['APP_INPUT_RECEIPT']:
    raise SystemExit('Downloaded app receipt does not match the producing job output')
r=json.loads(receipt)
if (r['commit'],r['runId'],str(r['runAttempt']))!=(os.environ['GITHUB_SHA'],os.environ['GITHUB_RUN_ID'],os.environ['GITHUB_RUN_ATTEMPT']):
    raise SystemExit('Downloaded app source/run/attempt mismatch')
for item in r['files']:
    relative=Path(item['path'])
    if relative.is_absolute() or '..' in relative.parts:raise SystemExit('Unsafe receipt path')
    path=directory/relative
    if path.is_symlink() or hashlib.sha256(path.read_bytes()).hexdigest()!=item['sha256']:
        raise SystemExit('App product integrity mismatch: '+str(relative))
print('APP_INPUT_VERIFIED '+r['commit'])
