"""Verify actual downloaded products before execution, not a capability checklist."""
import hashlib,json,os
from pathlib import Path
root=Path(__file__).resolve().parents[1];directory=root/'build/apps'
r=json.loads((directory/'receipt.json').read_text())
if (r['commit'],r['runId'],str(r['runAttempt']))!=(os.environ['GITHUB_SHA'],os.environ['GITHUB_RUN_ID'],os.environ['GITHUB_RUN_ATTEMPT']):
    raise SystemExit('Downloaded app source/run/attempt mismatch')
for item in r['files']:
    relative=Path(item['path'])
    if relative.is_absolute() or '..' in relative.parts:raise SystemExit('Unsafe receipt path')
    path=directory/relative
    if path.is_symlink() or hashlib.sha256(path.read_bytes()).hexdigest()!=item['sha256']:
        raise SystemExit('App product integrity mismatch: '+str(relative))
print('APP_INPUT_VERIFIED '+r['commit'])
