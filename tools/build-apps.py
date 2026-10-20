#!/usr/bin/env python3
"""Runner-internal fixture compilation. Public entry: build.sh only."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import plistlib
import shutil
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'build/apps'

def command(*args):
    try:return subprocess.check_output(list(map(str,args)),stderr=subprocess.STDOUT)
    except subprocess.CalledProcessError as error:
        OUT.mkdir(parents=True,exist_ok=True)
        with (OUT/'toolchain-error.log').open('ab') as stream:
            stream.write(('COMMAND '+json.dumps(list(map(str,args)))+'\n').encode())
            stream.write(error.output)
        sys.stdout.buffer.write(error.output);sys.stdout.buffer.flush()
        raise

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--app',default='all')
    args=parser.parse_args()
    if sys.platform!='darwin' or platform.machine() not in ('arm64','x86_64') or os.environ.get('GITHUB_ACTIONS')!='true':
        raise SystemExit('App compilation requires a hosted macOS SDK runner through build.sh')
    declarations={json.loads(path.read_text())['id']:path for path in sorted((ROOT/'apps').glob('*/app.json'))}
    selected=set(declarations) if args.app=='all' else set(args.app.split(','))
    if not selected or not selected<=set(declarations):parser.error('Unknown app selection')
    OUT.mkdir(parents=True,exist_ok=False)
    sdk=command('xcrun','--sdk','iphoneos','--show-sdk-path').decode().strip()
    firmware=json.loads((ROOT/'sources.lock.json').read_text())['iosUserland']
    minimum=firmware['version'].split('.')[0]+'.0'
    target='arm64-apple-ios'+minimum
    environment={'xcode':command('xcodebuild','-version').decode(),
                 'swift':command('xcrun','swiftc','--version').decode(),
                 'sdk':sdk,'target':target,'host':platform.platform(),'hostArchitecture':platform.machine()}
    (OUT/'environment.json').write_text(json.dumps(environment,indent=2)+'\n')
    command('xcrun','clang','-target',target,'-isysroot',sdk,'-dynamiclib',
            '-fblocks',ROOT/'src/ios-platform.c',ROOT/'src/ios-lifecycle.m',ROOT/'src/ios-scene-token.m',ROOT/'src/ios-rendering.m',ROOT/'src/ios-input.m',ROOT/'src/diagnostics/ios-lifecycle.m',ROOT/'src/diagnostics/ios-colors.m',
            '-o',OUT/'libarlinux-ios.dylib',
            '-framework','UIKit','-framework','Foundation','-framework','CoreFoundation','-framework','CoreGraphics','-framework','QuartzCore',
            '-Wl,-install_name,@loader_path/libarlinux-ios.dylib')
    products=[]
    for app_id in sorted(selected):
        declaration=declarations[app_id]
        app=json.loads(declaration.read_text()); name=app['name']; bundle=OUT/(name+'.app'); bundle.mkdir()
        info={'CFBundleExecutable':name,'CFBundleIdentifier':'io.arlinux.'+name,
              'CFBundleName':name,'CFBundleVersion':'1','CFBundleShortVersionString':app['version'],
              'CFBundlePackageType':'APPL','MinimumOSVersion':minimum,'UIDeviceFamily':[1,2],
              'NSAppTransportSecurity':{'NSAllowsLocalNetworking':True}}
        (bundle/'Info.plist').write_bytes(plistlib.dumps(info))
        if app['kind']=='application': shutil.copy2(OUT/'libarlinux-ios.dylib',bundle/'libarlinux-ios.dylib')
        record={**app,'binary':str((bundle/name).relative_to(ROOT)),
                'sourceSHA256':hashlib.sha256((declaration.parent/app['source']).read_bytes()).hexdigest()}
        log=OUT/(app['id']+'.build.log')
        def compile_source(source,destination,language,frameworks,extension=False):
            flags=['-target',target,'-sdk' if language=='swift' else '-isysroot',sdk,'-g','-O']
            if language=='swift': flags+=['-swift-version','5','-import-objc-header',str(ROOT/'apps/shared/Inspect.h')]
            else:
                flags+=['-Wall','-Wextra','-Werror','-Wno-deprecated-declarations']
                if language=='objc': flags+=['-fobjc-arc','-fblocks']
            if extension: flags+=['-fapplication-extension','-Wl,-e,_NSExtensionMain']
            extra=[]
            if app['kind']=='application':
                inspector=OUT/'inspection.o'
                if not inspector.exists():
                    command('xcrun','clang','-target',target,'-isysroot',sdk,'-fobjc-arc','-fblocks','-Wno-deprecated-declarations','-c',ROOT/'apps/shared/Inspect.m','-o',inspector)
                extra=[str(inspector)]
            args=['xcrun','swiftc' if language=='swift' else 'clang',*flags,str(source),*extra,'-o',str(destination)]
            for framework in frameworks: args+=['-framework',framework]
            result=subprocess.run(args,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
            with log.open('ab') as stream:
                stream.write(('COMMAND '+json.dumps(args)+'\n').encode());stream.write(result.stdout)
            if result.returncode: raise RuntimeError(result.stdout.decode(errors='replace')[-3000:])
        try:
            compile_source(declaration.parent/app['source'],bundle/name,app['language'],app['frameworks'])
            for helper in app.get('helpers',[]):
                folder=bundle/'PlugIns'/(helper['name']+'.appex');folder.mkdir(parents=True)
                (folder/'Info.plist').write_bytes(plistlib.dumps({'CFBundleExecutable':helper['name'],
                    'CFBundleName':helper['name'],'CFBundleDisplayName':helper['name'],
                    'CFBundleIdentifier':'io.arlinux.'+name+'.'+helper['name'],'CFBundlePackageType':'XPC!',
                    'CFBundleVersion':'1','CFBundleShortVersionString':'1.0','MinimumOSVersion':minimum,
                    'NSExtension':{'NSExtensionPointIdentifier':helper['point'],
                        'NSExtensionPrincipalClass':helper['principalClass'],
                        'NSExtensionAttributes':{'NSExtensionActivationRule':{'NSExtensionActivationSupportsText':True}}}}))
                compile_source(declaration.parent/helper['source'],folder/helper['name'],'objc',helper['frameworks'],extension=True)
                record['helper']=str((folder/helper['name']).relative_to(ROOT))
                record['helperRole']='application'
            record['build']='passed';record['binarySHA256']=hashlib.sha256((bundle/name).read_bytes()).hexdigest()
        except (OSError,RuntimeError) as error:
            record['build']='failed';record['error']=str(error)
        products.append(record)
        print('APP_BUILD '+json.dumps({'id':app['id'],'status':record['build'],'error':record.get('error','').splitlines()[:2] if record.get('error') else None}),flush=True)
    (OUT/'products.json').write_text(json.dumps({'source':os.environ['GITHUB_SHA'],'selection':args.app,
        'runId':os.environ['GITHUB_RUN_ID'],'apps':products},indent=2)+'\n')
    return 0 if all(app['build']=='passed' for app in products) else 1

if __name__=='__main__': raise SystemExit(main())
