#!/usr/bin/env python3
"""Runner-internal fixture compilation. Public entry: build.sh only."""
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
    return subprocess.check_output(list(map(str,args)),stderr=subprocess.STDOUT)

def main():
    if sys.platform!='darwin' or platform.machine()!='arm64' or os.environ.get('GITHUB_ACTIONS')!='true':
        raise SystemExit('App compilation requires the macOS ARM64 runner through build.sh')
    OUT.mkdir(parents=True,exist_ok=False)
    sdk=command('xcrun','--sdk','iphoneos','--show-sdk-path').decode().strip()
    target='arm64-apple-ios12.0'
    environment={'xcode':command('xcodebuild','-version').decode(),
                 'swift':command('xcrun','swiftc','--version').decode(),
                 'sdk':sdk,'target':target,'host':platform.platform()}
    (OUT/'environment.json').write_text(json.dumps(environment,indent=2)+'\n')
    command('xcrun','clang','-target',target,'-isysroot',sdk,'-dynamiclib',
            ROOT/'src/ios-platform.c','-o',OUT/'libarlinux-ios.dylib',
            '-framework','UIKit','-framework','CoreFoundation','-framework','CoreGraphics',
            '-Wl,-install_name,@loader_path/libarlinux-ios.dylib')
    products=[]
    for declaration in sorted((ROOT/'apps').glob('*/app.json')):
        app=json.loads(declaration.read_text()); name=app['name']; bundle=OUT/(name+'.app'); bundle.mkdir()
        info={'CFBundleExecutable':name,'CFBundleIdentifier':'io.arlinux.'+name,
              'CFBundleName':name,'CFBundleVersion':'1','CFBundleShortVersionString':app['version'],
              'CFBundlePackageType':'APPL','MinimumOSVersion':'12.0','UIDeviceFamily':[1,2],
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
                inspector=OUT/(app['id']+'-inspection.o')
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
                    'CFBundleVersion':'1','CFBundleShortVersionString':'1.0','MinimumOSVersion':'12.0',
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
    (OUT/'products.json').write_text(json.dumps({'source':os.environ['GITHUB_SHA'],
        'runId':os.environ['GITHUB_RUN_ID'],'apps':products},indent=2)+'\n')
    return 0 if all(app['build']=='passed' for app in products) else 1

if __name__=='__main__': raise SystemExit(main())
