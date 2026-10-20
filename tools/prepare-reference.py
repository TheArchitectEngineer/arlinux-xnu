"""Runner-internal PlayCover conversion for the same app collection."""
import json
from pathlib import Path
import plistlib
import shutil
import subprocess
ROOT=Path(__file__).resolve().parents[1];OUT=ROOT/'build/reference-tools';OUT.mkdir(parents=True,exist_ok=False)
def run(*args):subprocess.run(list(map(str,args)),check=True)
source=json.loads((ROOT/'sources.lock.json').read_text())['playcoverReference']
upstream=ROOT/'build/reference-source';upstream.mkdir()
run('git','-C',upstream,'init');run('git','-C',upstream,'fetch','--depth=1','https://github.com/'+source['repository']+'.git',source['commit'])
run('git','-C',upstream,'checkout','--detach','FETCH_HEAD')
# Retain corresponding GPL converter source and its license in safe evidence.
corresponding=ROOT/'build/results/converter-source';corresponding.mkdir(parents=True)
for name in ['PlayCover/Utils/Macho.swift','PlayCover/Utils/Extensions/DataExtensions.swift','PlayCover/PlayCoverError.swift','LICENSE']:
    shutil.copyfile(upstream/name,corresponding/Path(name).name)
for name in ['playcover-convert.swift','playcover-mach-bridge.h']:
    shutil.copyfile(ROOT/'tools'/name,corresponding/name)
run('xcrun','swiftc','-O','-import-objc-header',corresponding/'playcover-mach-bridge.h',corresponding/'Macho.swift',corresponding/'DataExtensions.swift',corresponding/'PlayCoverError.swift',corresponding/'playcover-convert.swift','-o',OUT/'convert')
run('xcrun','swiftc',ROOT/'tools/desktop-events.swift','-o',OUT/'desktop-events')
run('brew','install','tesseract')
products=json.loads((ROOT/'build/apps/products.json').read_text())
for app in products['apps']:
    if app['build']!='passed':continue
    original=ROOT/'build/apps'/(app['name']+'.app');bundle=ROOT/'build/reference-apps'/original.name
    shutil.copytree(original,bundle)
    platform=bundle/'libarlinux-ios.dylib'
    if platform.exists():platform.unlink()
    for binary in [bundle/app['name'],*[bundle/'PlugIns'/(h['name']+'.appex')/h['name'] for h in app.get('helpers',[])]]:
        run(OUT/'convert',binary);binary.chmod(0o755)
    info=plistlib.loads((bundle/'Info.plist').read_bytes());info['LSMinimumSystemVersion']='11.0'
    (bundle/'Info.plist').write_bytes(plistlib.dumps(info))
    run('codesign','--force','--deep','--sign','-',bundle)
