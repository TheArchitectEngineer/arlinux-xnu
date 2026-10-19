param([ValidateSet('build','status','download')][string]$Command='build',[string]$RunId)
$ErrorActionPreference='Stop'
$repo='taowen/arlinux-xnu'
switch($Command) {
 'build' { & gh workflow run experiment.yml -R $repo --ref main }
 'status' { if($RunId) { & gh run view $RunId -R $repo } else { & gh run list -R $repo --workflow experiment.yml --limit 5 } }
 'download' { if(-not $RunId){throw 'Pass -RunId for the exact experiment.'}; & gh run download $RunId -R $repo --dir (Join-Path $PSScriptRoot "build/runs/$RunId") }
}
if($LASTEXITCODE -ne 0){throw "gh failed: $LASTEXITCODE"}
