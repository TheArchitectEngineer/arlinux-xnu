#!/usr/bin/env python3
"""Compile actual XNU kernel translation units for the Linux ARM64 host port.

Not a mock or a pass/fail source inspection. Compiler failures are retained and
propagated. A compiled object is not a linked or booted userspace kernel.
"""
import argparse
import hashlib
import json
import os
import re
from pathlib import Path
import shlex
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
XNU = ROOT / 'third_party/xnu'
OUT = ROOT / 'build/kernel'


def run(command, cwd=ROOT, **kwargs):
    print('> ' + shlex.join(map(str, command)), flush=True)
    return subprocess.run(list(map(str, command)), cwd=cwd, check=True, **kwargs)


def upstream_words(makefile, variable):
    # These upstream declarations are literal filename lists, not Make recipes.
    text = makefile.read_text().replace('\\\n', ' ')
    match = re.search(r'^' + re.escape(variable) + r'\s*=([^\n]*)', text, re.M)
    if match is None:
        raise RuntimeError(f'Missing upstream declaration {variable} in {makefile}')
    words = match[1].partition('#')[0].split()
    if any('$' in word for word in words):
        raise RuntimeError(f'Nonliteral upstream declaration {variable}')
    return words


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--phase', choices=['prepare', 'compile', 'all'], default='all')
    parser.add_argument('--compiler', default='clang-18')
    args = parser.parse_args()
    OUT.mkdir(parents=True, exist_ok=True)
    lock = json.loads((ROOT / 'sources.lock.json').read_text())
    revision = subprocess.check_output(['git', '-C', str(XNU), 'rev-parse', 'HEAD'], text=True).strip()
    if revision != lock['xnuKernel']['commit']:
        raise RuntimeError('XNU source does not match the declared submodule pin')
    (OUT / 'sources.lock.json').write_text(json.dumps(lock, indent=2) + '\n')
    generated = OUT / 'config'
    exported = OUT / 'exported'
    if args.phase != 'compile':
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
        env = dict(os.environ, SRCROOT=str(XNU), OBJROOT=str(OUT), SOURCE=str(XNU / "osfmk/conf"))
        run(['tcsh', XNU / 'SETUP/config/doconf', '-verbose', '-c', '-cpu', 'arm64', '-soc', 'usermode',
             '-platform', 'iPhoneOS', '-d', generated, '-s', XNU / 'osfmk/conf',
             '-m', XNU / 'config', lock['xnuKernel']['configuration']], env=env)
        exported = OUT / 'exported'
        san = exported / 'san'
        san.mkdir(parents=True, exist_ok=True)
        for component in ['memory', 'coverage']:
            directory = XNU / 'san' / component
            for variable in ['PRIVATE_XNUFILES', 'KERNELFILES', 'PRIVATE_KERNELFILES']:
                for header in upstream_words(directory / 'Makefile', variable):
                    shutil.copyfile(directory / header, san / header)
        version_header = exported / 'libkern/version.h'
        version_header.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(XNU / 'libkern/libkern/version.h.template', version_header)
        run(['perl', XNU / 'config/newvers.pl', version_header],
            env=dict(env, CURRENT_KERNEL_CONFIG_LC=lock['xnuKernel']['configuration'].lower()))
    makefile = (generated / 'Makefile').read_text()
    definitions = next(line.partition('=')[2] for line in makefile.splitlines()
                       if line.startswith('export CONFIG_DEFINES ='))
    options = shlex.split(definitions)
    resource = subprocess.check_output([args.compiler, '-print-resource-dir'], text=True).strip()
    includes = [ROOT / 'src/xnu-host/include', generated, exported,
                XNU / 'osfmk', XNU / 'bsd', XNU / 'libkern', XNU / 'osfmk/libsa',
                XNU / 'pexpert', XNU / 'iokit', XNU / 'EXTERNAL_HEADERS', XNU]
    flags = [args.compiler, '--target=aarch64-linux-gnu', '-c', '-g', '-O1', '-ffreestanding',
             '-fno-builtin', '-fblocks', '-fno-common', '-ffunction-sections',
             '-fdata-sections', '-nostdinc', '-ferror-limit=8',
             '-mbranch-protection=none', '-include', str(generated / 'meta_features.h'),
             '-U__linux__', '-Ulinux', '-D__APPLE__=1', '-isystem', resource + '/include',
             '-D__arm64__=1', '-D__ARM64__=1', '-Darm64=1', '-DARM64=1',
             '-D__ARM_ARCH__=8', '-D__ARM_ARCH_8_0__=1', '-D__ARM64_ARCH_8__=1', '-D__ARM_16K_PG__=1',
             '-DPAGE_SIZE_FIXED=1', '-DAPPLE=1', '-DKERNEL=1', '-DKERNEL_PRIVATE=1',
             '-DXNU_KERNEL_PRIVATE=1', '-DMACH_KERNEL_PRIVATE=1', '-DMACH_KERNEL=1',
             '-DPRIVATE=1', '-D__MACHO__=1', '-DCONFIG_USERMODE=1',
             # Keep BSD syscall implementations out of Linux libc's namespace.
             # Otherwise native fault probes bind fork/setrlimit to kernel ABIs.
             '-Dfork=xnu_bsd_fork', '-Dsetrlimit=xnu_bsd_setrlimit',
             '-Dabort=xnu_kernel_abort',
             '-D__private_extern__=extern', *options, *['-I' + str(p) for p in includes]]
    if args.phase != 'compile':
        # MIG is a build-time compiler, not a replacement IPC implementation.
        mig_source = ROOT / lock['migCompiler']['path']
        mig_revision = subprocess.check_output(['git', '-C', str(mig_source), 'rev-parse', 'HEAD'], text=True).strip()
        if mig_revision != lock['migCompiler']['commit']:
            raise RuntimeError('MIG compiler source does not match its pin')
        mig_build = OUT / 'mig-tool'
        # Use Linux libc for the host tool and the actual XNU Mach ABI headers.
        host_flags = ' '.join(['-D__arm64__=1', '-D_BSD_ARM__TYPES_H_=1',
                               '-D__kernel_ptr_semantics=', '-D__LITTLE_ENDIAN__=1',
                               *['-idirafter ' + str(XNU / p) for p in
                                 ['osfmk', 'bsd', 'libkern', 'EXTERNAL_HEADERS']]])
        run(['cmake', '-S', mig_source, '-B', mig_build,
             '-DCMAKE_C_COMPILER=clang-18', '-DCMAKE_C_FLAGS=' + host_flags])
        run(['cmake', '--build', mig_build, '--target', 'migcom', '-j2'])
        mig_output = OUT / 'mig/mach'
        mig_output.mkdir(parents=True, exist_ok=True)
        # Native preprocessing supplies the selected guest ABI and kernel options;
        # the upstream MIG compiler consumes the resulting interface definitions.
        cpp_flags = [f for f in flags[1:] if f not in ['-c', '-g', '-O1']]
        mach_makefile = XNU / 'osfmk/mach/Makefile'
        senders = {name.removesuffix('_user.c') for name in upstream_words(mach_makefile, 'MIG_KUSRC')}
        senders |= {name.removesuffix('.h') for name in upstream_words(mach_makefile, 'MIG_KUHDRS')}
        public_senders = {name.removesuffix('.h') for name in
                          upstream_words(mach_makefile, 'MIG_UUHDRS')} - senders
        servers = {name.removesuffix('_server.c') for name in upstream_words(mach_makefile, 'MIG_KSSRC')}
        public_servers = {name.removesuffix('_server.h') for name in
                          upstream_words(mach_makefile, 'MIG_USHDRS')} - servers
        device_interfaces = {Path(name).stem for name in
                             upstream_words(XNU / 'osfmk/device/Makefile', 'MIG_DEFS')}
        families = [('mach', senders, servers, public_servers, public_senders),
                    ('device', set(), device_interfaces, set(), set())]
        for family, sender_interfaces, server_interfaces, public_headers, public_sender_headers in families:
            mig_output = OUT / 'mig' / family
            mig_output.mkdir(parents=True, exist_ok=True)
            for side, interfaces in [('user', sender_interfaces), ('server', server_interfaces),
                                     ('public', public_headers),
                                     ('public_user', public_sender_headers)]:
                for interface in sorted(interfaces):
                    definition = XNU / 'osfmk' / family / (interface + '.defs')
                    preprocessed = mig_output / (interface + '_' + side + '.defs.i')
                    with preprocessed.open('w') as stream:
                        run(['clang-18', '-E', '-x', 'c', *cpp_flags,
                             *(['-DKERNEL_' + side.upper() + '=1'] if side in ('user', 'server') else []),
                             '-D__MACH30__=1', definition], stdout=stream)
                    with preprocessed.open() as stream:
                        run([mig_build / 'migcom', '-novouchers',
                             *(['-maxonstack', '1024'] if side == 'user' else []),
                             '-header', mig_output / (interface + '.h') if side in ('user', 'public_user') else '/dev/null',
                             '-user', mig_output / (interface + '_user.c') if side == 'user' else '/dev/null',
                             '-sheader', mig_output / (interface + '_server.h') if side in ('server', 'public') else '/dev/null',
                             '-server', mig_output / (interface + '_server.c') if side == 'server' else '/dev/null'], stdin=stream)
        # Match bsd/sys/Makefile and MakeInc.rule: unifdef followed by the
        # original syscall generator. Preserve the generated input and headers.
        bsd_generated = OUT / 'bsd'
        bsd_generated.mkdir(exist_ok=True)
        master = bsd_generated / 'bsd.syscalls.master'
        with master.open('w') as stream:
            result = subprocess.run(['unifdef', str(XNU / 'bsd/kern/syscalls.master')],
                                    stdout=stream)
        if result.returncode not in (0, 1):
            result.check_returncode()  # unifdef exit 1 means changed, not failed.
        syscall_headers = exported / 'sys'
        syscall_headers.mkdir(exist_ok=True)
        for mode in ['proto', 'header']:
            run(['sh', XNU / 'bsd/kern/makesyscalls.sh', master, mode], cwd=syscall_headers)
    if args.phase == 'prepare':
        print('XNU configuration and MIG interfaces generated; no kernel objects compiled', flush=True)
        return
    flags += ['-I' + str(OUT / 'mig')]
    # These are actual intended kernel objects. Do not replace them with dummy
    # implementations to turn the workflow green.
    units = [
        'src/xnu-host/xnu_bootstrap.c', 'src/xnu-host/context.c', 'src/xnu-host/platform.c',
        'pexpert/gen/device_tree.c',
        'src/xnu-host/xnu_vm_bootstrap.c', 'src/xnu-host/pmap.c', 'src/xnu-host/time.c',
        'src/xnu-host/entropy.c', 'src/xnu-host/image.c', 'src/xnu-host/ro_zone.c',
        'src/xnu-host/ticket.c', 'osfmk/arm64/usermode_ticket.S',
        'src/xnu-host/copyio_probe.c', 'osfmk/arm64/copyio.c',
        'osfmk/arm64/usermode_copyio.S',
        'osfmk/arm64/usermode_cswitch.S',
        # Core IPC structures used by Mach ports/messages and task initialization.
        *['osfmk/ipc/' + name + '.c' for name in [
            'ipc_space', 'mach_port', 'mach_msg', 'ipc_init', 'ipc_entry',
            'ipc_hash', 'ipc_object', 'ipc_port', 'ipc_pset', 'ipc_right',
            'ipc_mqueue', 'ipc_kmsg', 'ipc_notify', 'ipc_importance',
            'ipc_service_port', 'ipc_voucher', 'ipc_eventlink', 'mach_debug']],
        # Real kernel initialization and allocation: no host malloc stand-ins.
        *['osfmk/kern/' + name + '.c' for name in [
            'startup', 'task', 'thread', 'exception', 'ipc_tt', 'ipc_kobject',
            'ipc_host', 'ipc_mig', 'host', 'processor', 'kalloc', 'zalloc',
            'lock_group', 'locks', 'compact_id', 'lock_mtx', 'lock_rw', 'lock_ptr',
            'lock_ticket', 'ledger', 'kext_alloc', 'backtrace', 'btlog',
            'timer', 'bsd_kern', 'kern_cdata', 'machine',
            # Providers required by the actual VM/thread link, not host stubs.
            'ast', 'sched_prim', 'sched_average', 'sched_traditional', 'sched_dualq', 'priority',
            'waitq', 'turnstile', 'timer_call', 'mpsc_queue', 'recount',
            'thread_act', 'thread_policy', 'task_policy', 'syscall_subr',
            'restartable', 'work_interval', 'smr', 'thread_call', 'stack', 'task_ref', 'clock',
            'coalition', 'extmod_statistics', 'host_notify', 'counter_common',
            'clock_oldops', 'sync_sema', 'task_ident', 'ipc_clock',
            'affinity', 'thread_group', 'sched_multiq', 'ux_handler', 'mk_timer', 'mk_sp']],
        # Required VM bootstrap, maps, objects, residency, faults and kernel maps.
        *['osfmk/vm/' + name + '.c' for name in [
            'vm_init', 'vm_map', 'vm_user', 'vm_object', 'vm_resident',
            'vm_fault', 'vm_kern', 'memory_object', 'vm_pageout',
            'vm_map_store', 'vm_map_store_ll', 'vm_map_store_rb', 'vm_purgeable',
            'vm32_user', 'vm_compressor', 'vm_compressor_pager', 'vm_compressor_algorithms',
            'vm_compressor_backing_store', 'lz4']],
        'osfmk/arm64/WKdmCompress_16k.s', 'osfmk/arm64/WKdmDecompress_16k.s',
        'osfmk/arm64/WKdmData.s', 'src/xnu-host/codec_probe.c',
        'osfmk/arm/locks_arm.c', 'osfmk/arm/rtclock.c', 'osfmk/arm/arm_timer.c',
        'osfmk/arm/machine_routines_common.c',
        'osfmk/arm64/pcb.c', 'osfmk/arm64/loose_ends.c',
        'osfmk/arm64/status.c', 'osfmk/arm64/machine_task.c',
        'libkern/os/refcnt.c', 'libkern/gen/OSAtomicOperations.c',
        'osfmk/bank/bank.c', 'libkern/c++/priority_queue.cpp',
        'libkern/libclosure/runtime.cpp', 'libkern/libclosure/libclosuredata.c',
        'osfmk/prng/prng_random.c', 'osfmk/arm/counter.c',
        'bsd/kern/kern_fork.c', 'bsd/kern/kern_proc.c',
        'bsd/kern/kern_credential.c', 'bsd/kern/kern_prot.c',
        'bsd/kern/kern_resource.c', 'bsd/uxkern/ux_exception.c',
        'bsd/kern/kern_authorization.c', 'bsd/kern/kern_priv.c',
        'bsd/kern/kern_subr.c', 'bsd/kern/bsd_init.c',
        'bsd/kern/kern_synch.c', 'bsd/kern/kern_newsysctl.c',
    ]
    # Build the actual MIG dispatch implementations, not just their headers.
    configured_objects = set(upstream_words(generated / 'Makefile', 'OBJS'))
    units += ['build/kernel/mig/mach/' + name for name in
              upstream_words(XNU / 'osfmk/mach/Makefile', 'MIG_KSSRC')
              if Path(name).with_suffix('.o').name in configured_objects]
    units += ['build/kernel/mig/mach/' + name for name in
              upstream_words(XNU / 'osfmk/mach/Makefile', 'MIG_KUSRC')
              if Path(name).with_suffix('.o').name in configured_objects]
    units += ['build/kernel/mig/device/' + name for name in
              upstream_words(XNU / 'osfmk/device/Makefile', 'DEVICE_FILES')
              if name.endswith('.c')]
    commands, results = [], []
    for unit in units:
        name = Path(unit).stem
        obj = OUT / (name + '.o')
        source_path = ROOT / unit if unit.startswith(('src/', 'build/')) else XNU / unit
        component_flags = []
        if source_path.suffix == '.s':
            component_flags += ['-x', 'assembler-with-cpp']
        if unit.startswith('libkern/'):
            # libkern/conf/Makefile.template uses the BSD/libkern namespace.
            component_flags = ['-UMACH_KERNEL', '-UMACH_KERNEL_PRIVATE',
                               '-DLIBKERN_KERNEL_PRIVATE=1', '-DOSALLOCDEBUG=1']
        if unit.startswith('pexpert/'):
            # pexpert/conf/Makefile.template does not use osfmk's namespace.
            component_flags += ['-UMACH_KERNEL', '-UMACH_KERNEL_PRIVATE',
                                '-DPEXPERT_KERNEL_PRIVATE=1']
        if unit.startswith('bsd/'):
            # bsd/conf/Makefile.template's namespace and build flags.
            component_flags += ['-UMACH_KERNEL_PRIVATE', '-DBSD_KERNEL_PRIVATE=1',
                                '-DDRIVER_PRIVATE=1', '-D_KERNEL_BUILD=1',
                                '-DKERNEL_BUILD=1', '-DBSD_BUILD=1', '-DLP64_DEBUG=0']
        if source_path.suffix == '.cpp':
            component_flags += ['-std=gnu++17', '-fno-exceptions', '-fno-rtti']
        command = [*flags, *component_flags, str(source_path), '-o', str(obj)]
        commands.append({'directory': str(ROOT), 'file': str(source_path), 'arguments': command})
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
