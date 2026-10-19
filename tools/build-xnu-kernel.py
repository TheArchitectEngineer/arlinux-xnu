#!/usr/bin/env python3
"""Compile actual XNU kernel translation units for the Linux ARM64 host port.

Not a mock or a pass/fail source inspection. Compiler failures are retained and
propagated. A compiled object is not a linked or booted userspace kernel.
"""
import hashlib
import json
import os
from pathlib import Path
import shlex
import subprocess

ROOT = Path(__file__).resolve().parents[1]
XNU = ROOT / 'third_party/xnu'
OUT = ROOT / 'build/kernel'


def run(command, cwd=ROOT, **kwargs):
    print('> ' + shlex.join(map(str, command)), flush=True)
    return subprocess.run(list(map(str, command)), cwd=cwd, check=True, **kwargs)


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    lock = json.loads((ROOT / 'sources.lock.json').read_text())
    revision = subprocess.check_output(['git', '-C', str(XNU), 'rev-parse', 'HEAD'], text=True).strip()
    if revision != lock['xnuKernel']['commit']:
        raise RuntimeError('XNU source does not match the declared submodule pin')
    (OUT / 'sources.lock.json').write_text(json.dumps(lock, indent=2) + '\n')
    setup = OUT / 'SETUP/config'
    setup.mkdir(parents=True, exist_ok=True)
    source = XNU / 'SETUP/config'
    run(['bison', '-y', '-d', '-o', 'parser.c', source / 'parser.y'], cwd=setup)
    run(['flex', '--header-file=lexer.yy.h', '-o', 'lexer.yy.c', source / 'lexer.l'], cwd=setup)
    config_sources = [source / (name + '.c') for name in
                      ['externs', 'main', 'mkheaders', 'mkioconf', 'mkmakefile', 'openp', 'searchp']]
    run(['clang-18', '-g', '-O0', '-DYY_NO_INPUT', '-I' + str(source), '-I.',
         *config_sources, 'parser.c', 'lexer.yy.c', '-o', 'config'], cwd=setup)
    generated = OUT / 'config'
    generated.mkdir(exist_ok=True)
    env = dict(os.environ, SRCROOT=str(XNU), OBJROOT=str(OUT))
    run(['tcsh', XNU / 'SETUP/config/doconf', '-c', '-cpu', 'arm64', '-soc', 'usermode',
         '-platform', 'iPhoneOS', '-d', generated, '-s', XNU / 'osfmk/conf',
         '-m', XNU / 'config', 'DEVELOPMENT'], env=env)
    makefile = (generated / 'Makefile').read_text()
    definitions = next(line.partition('=')[2] for line in makefile.splitlines()
                       if line.startswith('export CONFIG_DEFINES ='))
    # The XNU config generator's definitions, not a hand-maintained header list.
    options = shlex.split(definitions)
    include = OUT / 'include'
    (include / 'mach').mkdir(parents=True, exist_ok=True)
    for path, target in [(include / 'machine', XNU / 'osfmk/arm'),
                         (include / 'mach/machine', XNU / 'osfmk/mach/arm')]:
        if not path.exists():
            path.symlink_to(target, target_is_directory=True)
    resource = subprocess.check_output(['clang-18', '-print-resource-dir'], text=True).strip()
    includes = [ROOT / 'src/xnu-host/include', generated, include,
                XNU / 'osfmk', XNU / 'bsd', XNU / 'libkern', XNU / 'osfmk/libsa',
                XNU / 'pexpert', XNU / 'iokit', XNU / 'EXTERNAL_HEADERS', XNU]
    flags = ['clang-18', '--target=aarch64-linux-gnu', '-c', '-g', '-O1', '-ffreestanding',
             '-fno-builtin', '-fblocks', '-fno-common', '-nostdinc', '-ferror-limit=8',
             '-mbranch-protection=none', '-isystem', resource + '/include',
             '-D__arm64__=1', '-D__ARM64__=1', '-Darm64=1', '-DARM64=1',
             '-D__ARM_ARCH_8_0__=1', '-D__ARM64_ARCH_8__=1', '-D__ARM_16K_PG__=1',
             '-DPAGE_SIZE_FIXED=1', '-DAPPLE=1', '-DKERNEL=1', '-DKERNEL_PRIVATE=1',
             '-DXNU_KERNEL_PRIVATE=1', '-DMACH_KERNEL_PRIVATE=1', '-DMACH_KERNEL=1',
             '-DPRIVATE=1', '-D__MACHO__=1', '-DCONFIG_USERMODE=1',
             '-D__private_extern__=extern', *options, *['-I' + str(p) for p in includes]]
    # These are actual intended kernel objects. Do not replace them with dummy
    # implementations to turn the workflow green.
    units = ['osfmk/ipc/ipc_space.c', 'osfmk/ipc/mach_port.c', 'osfmk/ipc/mach_msg.c',
             'osfmk/kern/task.c', 'osfmk/kern/thread.c', 'osfmk/kern/exception.c',
             'osfmk/vm/vm_map.c', 'osfmk/vm/vm_user.c']
    commands, results = [], []
    for unit in units:
        name = Path(unit).stem
        obj = OUT / (name + '.o')
        command = [*flags, str(XNU / unit), '-o', str(obj)]
        commands.append({'directory': str(ROOT), 'file': str(XNU / unit), 'arguments': command})
        print('Compiling actual XNU:', unit, flush=True)
        completed = subprocess.run(command, capture_output=True, text=True)
        log = completed.stdout + completed.stderr
        (OUT / (name + '.log')).write_text(log)
        print(log, flush=True)
        entry = {'source': unit, 'exitCode': completed.returncode}
        if completed.returncode == 0:
            entry['objectSHA256'] = hashlib.sha256(obj.read_bytes()).hexdigest()
        results.append(entry)
    (OUT / 'compile_commands.json').write_text(json.dumps(commands, indent=2) + '\n')
    (OUT / 'compile-results.json').write_text(json.dumps(results, indent=2) + '\n')
    if any(item['exitCode'] != 0 for item in results):
        raise SystemExit('Actual XNU kernel compilation failed; inspect the retained per-unit logs')
    print('XNU objects compiled; linkage, boot and guest execution remain unverified', flush=True)


if __name__ == '__main__':
    main()
