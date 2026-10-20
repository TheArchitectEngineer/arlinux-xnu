"""Runner-only helpers for the existing real-application LLDB diagnosis."""
from pathlib import Path
import json
import re
import struct
import time
import lldb


def diagnostic_symbols():
    path = Path('build/dyld/diagnostic-symbols.json')
    return json.loads(path.read_text()) if path.exists() else []


def service_lookup(frame, location, internal_dict):
    """Observe the actual hit thread, without calling guest functions."""
    process = frame.GetThread().GetProcess()
    error = lldb.SBError()
    name_address = frame.FindRegister('x1').GetValueAsUnsigned()
    name = process.ReadCStringFromMemory(name_address, 256, error)
    print('SERVICE_LOOKUP ' + json.dumps({
        'tid': frame.GetThread().GetThreadID(), 'pc': hex(frame.GetPC()),
        'caller': hex(frame.FindRegister('lr').GetValueAsUnsigned()),
        'bootstrapPort': frame.FindRegister('x0').GetValueAsUnsigned(),
        'name': name if error.Success() else None,
        'nameReadError': None if error.Success() else str(error),
        'outputAddress': hex(frame.FindRegister('x2').GetValueAsUnsigned()),
    }), flush=True)
    if location.GetBreakpoint().GetHitCount() >= 32:
        location.GetBreakpoint().SetEnabled(False)
        print('SERVICE_LOOKUP_LIMIT reached; further lookup tracing disabled', flush=True)
    return False


def service_register(frame, location, internal_dict):
    process = frame.GetThread().GetProcess()
    error = lldb.SBError()
    name = process.ReadCStringFromMemory(frame.FindRegister('x1').GetValueAsUnsigned(), 256, error)
    print('SERVICE_REGISTER ' + json.dumps({
        'tid': frame.GetThread().GetThreadID(), 'pc': hex(frame.GetPC()),
        'caller': hex(frame.FindRegister('lr').GetValueAsUnsigned()),
        'bootstrapPort': frame.FindRegister('x0').GetValueAsUnsigned(),
        'name': name if error.Success() else None,
        'nameReadError': None if error.Success() else str(error),
        'servicePort': frame.FindRegister('x2').GetValueAsUnsigned(),
    }), flush=True)
    if location.GetBreakpoint().GetHitCount() >= 32:
        location.GetBreakpoint().SetEnabled(False)
    return False


def graphics_failure(frame, location, internal_dict):
    """Preserve the original error before stackshot reporting can block."""
    process = frame.GetThread().GetProcess()
    error = lldb.SBError()
    message = process.ReadCStringFromMemory(frame.FindRegister('x0').GetValueAsUnsigned(), 2048, error)
    print('GRAPHICS_FAILURE ' + json.dumps({
        'tid': frame.GetThread().GetThreadID(), 'pc': hex(frame.GetPC()),
        'caller': hex(frame.FindRegister('lr').GetValueAsUnsigned()),
        'message': message if error.Success() else None,
        'readError': None if error.Success() else str(error),
    }), flush=True)
    location.GetBreakpoint().SetEnabled(False)
    return False


_security_entries = {}
_ipc_callback_entries = {}


def ipc_callback(frame, location, internal_dict):
    """Observe real send/continuation/reply arguments without guest execution."""
    breakpoint = location.GetBreakpoint()
    symbol = _ipc_callback_entries[breakpoint.GetID()]
    objects = GuestObjects(frame.GetThread().GetProcess())
    arguments = [frame.FindRegister('x' + str(i)).GetValueAsUnsigned() for i in range(4)]
    record = {'symbol': symbol, 'tid': frame.GetThread().GetThreadID(),
              'caller': hex(frame.FindRegister('lr').GetValueAsUnsigned()),
              'arguments': [hex(value) for value in arguments], 'objects': []}
    for argument, pointer in enumerate(arguments[:3]):
        if pointer < 4096:
            continue
        item = {'argument': argument, 'address': hex(pointer)}
        try:
            item['words'] = [hex(value) for value in struct.unpack('<'+'Q'*32, objects.read(pointer, 256))]
        except (ValueError, SystemError) as error:
            item['readError'] = str(error)
        record['objects'].append(item)
    if symbol == '__dispatch_mach_msg_async_reply_invoke$VARIANT$mp':
        try:
            payload = objects.u64(arguments[0]+48)
            record['continuationMessage'] = {'address': hex(payload),
                'words': [hex(value) for value in struct.unpack('<'+'Q'*32, objects.read(payload, 256))]}
        except (ValueError, SystemError) as error:
            record['continuationReadError'] = str(error)
    print('IPC_CALLBACK ' + json.dumps(record), flush=True)
    if symbol == '__exit':
        frame.GetThread().GetProcess().GetTarget().GetDebugger().HandleCommand('thread backtrace --count 12')
    if breakpoint.GetHitCount() == 1:
        address = frame.GetPC()
        print('IPC_CALLBACK_CODE', hex(address), symbol, flush=True)
        frame.GetThread().GetProcess().GetTarget().GetDebugger().HandleCommand(
            'disassemble --start-address ' + hex(address) + ' --count 160')
        if symbol == '__dispatch_mach_msg_async_reply_invoke$VARIANT$mp':
            # Follow the actual ADRP/ADD/LDR/LDR indirect hook call site. This
            # uses the mapped instructions' offsets, not a guessed table ABI.
            words = struct.unpack('<'+'I'*160, objects.read(address, 640))
            for index in range(len(words)-3):
                adrp, add, load, hook = words[index:index+4]
                if adrp & 0x9f000000 != 0x90000000 or add & 0xffc00000 != 0x91000000:
                    continue
                register = adrp & 31
                if add & 31 != register or (add >> 5) & 31 != register:
                    continue
                if load & 0xffc00000 != 0xf9400000 or (load >> 5) & 31 != register:
                    continue
                if hook & 0xffc00000 != 0xf9400000 or (hook >> 5) & 31 != load & 31:
                    continue
                immediate = ((adrp >> 29) & 3) | (((adrp >> 5) & 0x7ffff) << 2)
                if immediate & (1 << 20): immediate -= 1 << 21
                pointer = ((address+index*4)&~4095)+(immediate<<12)+((add>>10)&4095)
                try:
                    table = objects.u64(pointer+((load>>10)&4095)*8)
                    target_address = objects.u64(table+((hook>>10)&4095)*8)
                    print('IPC_INDIRECT_CALLBACK_CODE', hex(target_address), flush=True)
                    frame.GetThread().GetProcess().GetTarget().GetDebugger().HandleCommand(
                        'disassemble --start-address ' + hex(target_address) + ' --count 160')
                except (ValueError, SystemError) as error:
                    print('IPC_INDIRECT_CALLBACK_READ_ERROR', str(error), flush=True)
    if breakpoint.GetHitCount() >= 16:
        breakpoint.SetEnabled(False)
    return False


def security_query(frame, location, internal_dict):
    process = frame.GetThread().GetProcess()
    breakpoint = location.GetBreakpoint()
    symbol = _security_entries[breakpoint.GetID()]
    registers = [frame.FindRegister('x' + str(i)).GetValueAsUnsigned() for i in range(3)]
    caller = frame.FindRegister('lr').GetValueAsUnsigned()
    record = {'symbol': symbol, 'tid': frame.GetThread().GetThreadID(),
              'caller': hex(caller), 'arguments': [hex(value) for value in registers]}
    if symbol == '_SecTaskCopyValueForEntitlement':
        try:
            record['entitlement'] = GuestObjects(process).cfstring(registers[1])
        except (ValueError, UnicodeError, SystemError) as error:
            record['entitlementReadError'] = str(error)
    print('SECURITY_QUERY ' + json.dumps(record), flush=True)
    # Do not install Python callbacks recursively from a callback. LLDB 18
    # corrupts its autogenerated callback namespace on that path (run
    # 39378124844). Entry observations suffice for this resolved query path.
    if breakpoint.GetHitCount() >= 16:
        breakpoint.SetEnabled(False)
    return False


def dictionary_inputs(frame, location, internal_dict):
    """Read actual ObjC ABI arguments only when a dictionary contains nil."""
    objects = GuestObjects(frame.GetThread().GetProcess())
    values, keys, count = [frame.FindRegister('x' + str(i)).GetValueAsUnsigned() for i in (2, 3, 4)]
    if 0 < count <= 16 and values and keys:
        try:
            pairs = [(objects.u64(keys + i*8), objects.u64(values + i*8)) for i in range(count)]
            if any(not value for key, value in pairs):
                record = {'tid': frame.GetThread().GetThreadID(),
                          'caller': hex(frame.FindRegister('lr').GetValueAsUnsigned()), 'pairs': []}
                for key, value in pairs:
                    pair = {'key': hex(key), 'value': hex(value)}
                    try:
                        pair['keyText'] = objects.cfstring(key)
                    except (ValueError, UnicodeError, SystemError) as error:
                        pair['keyReadError'] = str(error)
                    record['pairs'].append(pair)
                print('DICTIONARY_NIL_INPUT ' + json.dumps(record), flush=True)
                location.GetBreakpoint().SetEnabled(False)
        except (ValueError, SystemError) as error:
            print('DICTIONARY_INPUT_READ_ERROR', str(error), flush=True)
    if location.GetBreakpoint().GetHitCount() >= 4096:
        location.GetBreakpoint().SetEnabled(False)
    return False


def install_service_breakpoints(target):
    for symbol in diagnostic_symbols():
        if symbol.get('symbol') in ('__exit', '__xpc_connection_call_reply_async',
                '_dispatch_mach_send_with_result_and_async_reply_4libxpc',
                '__dispatch_mach_msg_async_reply_invoke$VARIANT$mp'):
            breakpoint = target.BreakpointCreateByAddress(int(symbol['address'], 16))
            _ipc_callback_entries[breakpoint.GetID()] = symbol['symbol']
            breakpoint.SetScriptCallbackFunction('dyld_debugger.ipc_callback')
            breakpoint.SetAutoContinue(True)
            print('IPC_CALLBACK_BREAKPOINT', symbol['symbol'], symbol['address'], flush=True)
        elif symbol.get('symbol') == '_OBJC_CLASS_$_NSDictionary':
            objects = GuestObjects(target.GetProcess())
            try:
                cls = objects.object_class(int(symbol['address'], 16))
                methods = objects.u64(objects.class_ro(cls)+32)
                size, count = objects.u32(methods) & ~3, objects.u32(methods+4)
                if size != 24 or count > 256:
                    raise ValueError('Unsupported dictionary method metadata')
                for index in range(count):
                    entry = methods+8+index*size
                    if objects.cstring(objects.u64(entry)) == 'dictionaryWithObjects:forKeys:count:':
                        address = objects.u64(entry+16)
                        breakpoint = target.BreakpointCreateByAddress(address)
                        breakpoint.SetScriptCallbackFunction('dyld_debugger.dictionary_inputs')
                        breakpoint.SetAutoContinue(True)
                        print('DICTIONARY_INPUT_BREAKPOINT', hex(address), flush=True)
            except (ValueError, SystemError) as error:
                print('DICTIONARY_METADATA_ERROR', str(error), flush=True)
        elif symbol.get('symbol') in ('_bootstrap_look_up', '_bootstrap_look_up2', '_bootstrap_look_up3'):
            breakpoint = target.BreakpointCreateByAddress(int(symbol['address'], 16))
            breakpoint.SetScriptCallbackFunction('dyld_debugger.service_lookup')
            breakpoint.SetAutoContinue(True)
            print('SERVICE_LOOKUP_BREAKPOINT', symbol['symbol'], symbol['address'], flush=True)
        elif symbol.get('symbol') in ('_bootstrap_register', '_bootstrap_register2'):
            breakpoint = target.BreakpointCreateByAddress(int(symbol['address'], 16))
            breakpoint.SetScriptCallbackFunction('dyld_debugger.service_register')
            breakpoint.SetAutoContinue(True)
            print('SERVICE_REGISTER_BREAKPOINT', symbol['symbol'], symbol['address'], flush=True)
        elif symbol.get('symbol') == '__GSWriteStackshot':
            breakpoint = target.BreakpointCreateByAddress(int(symbol['address'], 16))
            breakpoint.SetScriptCallbackFunction('dyld_debugger.graphics_failure')
            breakpoint.SetAutoContinue(True)
            print('GRAPHICS_FAILURE_BREAKPOINT', symbol['symbol'], symbol['address'], flush=True)
        elif symbol.get('symbol') in ('_SecTaskCreateFromSelf', '_SecTaskCopyValueForEntitlement'):
            breakpoint = target.BreakpointCreateByAddress(int(symbol['address'], 16))
            _security_entries[breakpoint.GetID()] = symbol['symbol']
            breakpoint.SetScriptCallbackFunction('dyld_debugger.security_query')
            breakpoint.SetAutoContinue(True)
            print('SECURITY_BREAKPOINT', symbol['symbol'], symbol['address'], flush=True)


def capture_code_references(target, address, count, label):
    objects = GuestObjects(target.GetProcess())
    words = struct.unpack('<' + 'I'*count, objects.read(address, count*4))
    for index, word in enumerate(words[:-1]):
        following = words[index+1]
        # Adjacent AArch64 ADRP + ADD (immediate), using the same base.
        if word & 0x9f000000 != 0x90000000 or following & 0xff000000 != 0x91000000:
            continue
        if (word & 31) != ((following >> 5) & 31):
            continue
        immediate = ((word >> 29) & 3) | (((word >> 5) & 0x7ffff) << 2)
        if immediate & (1 << 20):
            immediate -= 1 << 21
        pointer = ((address+index*4) & ~4095) + (immediate << 12)
        pointer += ((following >> 10) & 4095) << (12 if following & (1 << 22) else 0)
        text = None
        try:
            text = objects.cfstring(pointer)
        except (ValueError, UnicodeError, SystemError):
            try:
                # Arbitrary references may be binary blocks, not strings.
                # LLDB 18's CString SWIG binding can raise SystemError on
                # such bytes; inspect bounded raw memory instead.
                candidate = objects.read(pointer, 256).split(b'\0', 1)[0].decode('ascii')
                if 3 <= len(candidate) <= 256 and all(32 <= ord(c) < 127 for c in candidate):
                    text = candidate
            except (ValueError, UnicodeError, SystemError):
                pass
        if text:
            print(label + ' ' + json.dumps({'pc': hex(address+index*4), 'address': hex(pointer), 'text': text}), flush=True)


def capture_display_code(target):
    captured = set()

    def calls(address, count):
        instructions = target.ReadInstructions(target.ResolveLoadAddress(address), count)
        for instruction in instructions:
            if instruction.GetMnemonic(target) == 'bl':
                operand = instruction.GetOperands(target).split()[0]
                if re.fullmatch('0x[0-9a-fA-F]+', operand):
                    yield int(operand, 16)

    def disassemble(label, address, count):
        if address in captured:
            return
        captured.add(address)
        print(label, hex(address), flush=True)
        target.GetDebugger().HandleCommand('disassemble --start-address ' + hex(address) + ' --count ' + str(count))

    def references(address, count):
        capture_code_references(target, address, count, 'DISPLAY_REFERENCE')

    def mode_constants(address):
        objects = GuestObjects(target.GetProcess())
        words = struct.unpack('<'+'I'*24, objects.read(address, 24*4))
        for index, word in enumerate(words[:-1]):
            load = words[index+1]
            if word & 0x9f000000 != 0x90000000 or load & 0xffc00000 != 0xfd400000:
                continue
            if (word & 31) != ((load >> 5) & 31):
                continue
            immediate = ((word >> 29) & 3) | (((word >> 5) & 0x7ffff) << 2)
            if immediate & (1 << 20): immediate -= 1 << 21
            pointer = ((address+index*4)&~4095)+(immediate<<12)+(((load>>10)&4095)*8)
            value = struct.unpack('<d', objects.read(pointer, 8))[0]
            print('DISPLAY_MODE_RATE_FACTOR ' + json.dumps({'pc': hex(address+index*4), 'address': hex(pointer), 'factor': value}), flush=True)

    for symbol in diagnostic_symbols():
        if symbol.get('symbol') == '_BKSDisplayServicesStart':
            address = int(symbol['address'], 16)
            disassemble('DISPLAY_START_CODE', address, 120)
            query = next(calls(address, 16), None)
            startup_calls = list(calls(address, 24))
            if len(startup_calls) > 1:
                disassemble('DISPLAY_CONFIGURATION_CODE', startup_calls[1], 96)
            if query:
                disassemble('DISPLAY_START_QUERY_CODE', query, 40)
                # Inspect the actual port getter and MIG client called by the
                # observed query. No firmware-specific addresses are assumed.
                for child in list(dict.fromkeys(calls(query, 32)))[:4]:
                    disassemble('DISPLAY_QUERY_CALLEE_CODE', child, 180)
        elif symbol.get('symbol') == '__GSRegisterPurpleNamedPortInPrivateNamespace':
            address = int(symbol['address'], 16)
            disassemble('EVENT_PORT_REGISTER_CODE', address, 200)
            for child in list(dict.fromkeys(calls(address, 110)))[:8]:
                disassemble('EVENT_PORT_REGISTER_CALLEE_CODE', child, 100)
        elif symbol.get('symbol') == '_bootstrap_look_up3':
            disassemble('BOOTSTRAP_LOOKUP_CODE', int(symbol['address'], 16), 160)
        elif symbol.get('symbol') in ('_OBJC_CLASS_$_CADisplay', '_OBJC_CLASS_$_CADisplayMode'):
            objects = GuestObjects(target.GetProcess())
            address = int(symbol['address'], 16)
            try:
                for cls, kind in ((address, 'instance'), (objects.object_class(address), 'class')):
                    methods = objects.u64(objects.class_ro(cls)+32)
                    if not methods:
                        continue
                    # objc4-750.1 method_list_t uses the low two bits for
                    # fixup flags; they are not part of the entry size.
                    entry_size, count = objects.u32(methods) & ~3, objects.u32(methods+4)
                    if entry_size != 24 or count > 256:
                        raise ValueError(f'Unsupported CADisplay method metadata: size={entry_size} count={count}')
                    for index in range(count):
                        entry = methods+8+index*entry_size
                        name = objects.cstring(objects.u64(entry))
                        implementation = objects.u64(entry+16)
                        print('OBJC_DISPLAY_METHOD ' + json.dumps({'class': symbol['symbol'], 'kind': kind, 'selector': name, 'address': hex(implementation)}), flush=True)
                        if symbol['symbol'] == '_OBJC_CLASS_$_CADisplayMode' and name == 'refreshRate':
                            mode_constants(implementation)
                        if name in ('mainDisplay', 'displays', 'bounds', 'currentMode', 'availableModes', 'preferredMode', 'width', 'height', 'refreshRate') or name.lstrip('_').startswith('initWith'):
                            disassemble('DISPLAY_METHOD_CODE', implementation, 96)
                            for child_index, child in enumerate(list(dict.fromkeys(calls(implementation, 48)))[:3]):
                                if kind == 'class' and name in ('mainDisplay', 'displays'):
                                    disassemble('DISPLAY_METHOD_CALLEE_CODE', child, 400)
                                    for grandchild in list(dict.fromkeys(calls(child, 64)))[:6]:
                                        disassemble('DISPLAY_METHOD_CALLEE_CODE', grandchild, 180)
                                    references(child, 400)
                                else:
                                    count = 1320 if symbol['symbol'] == '_OBJC_CLASS_$_CADisplay' and name.lstrip('_').startswith('initWith') and child_index == 1 else 96
                                    disassemble('DISPLAY_METHOD_CALLEE_CODE', child, count)
                                    if count > 96:
                                        references(child, count)
            except (ValueError, UnicodeError, SystemError) as error:
                print('OBJC_DISPLAY_DECODE_ERROR', str(error), flush=True)


def install_exception_breakpoint(target):
    path = Path('build/dyld/diagnostic-symbols.json')
    if path.exists():
        for symbol in json.loads(path.read_text()):
            if symbol.get('symbol') == '_objc_exception_throw':
                address = int(symbol['address'], 16)
                breakpoint = target.BreakpointCreateByAddress(address)
                print('OBJC_EXCEPTION_BREAKPOINT', hex(address), breakpoint.GetID(), flush=True)
                return breakpoint
    print('OBJC_EXCEPTION_BREAKPOINT unavailable in actual cache symbols', flush=True)
    return None


class GuestObjects:
    """Read-only LP64 metadata decoder, not an injected Objective-C method call.

    Layout references: Apple objc4-750.1 isa.h/objc-runtime-new.h and
    CF-1153.18 CFString.c. Bounds/unknown classes fail explicitly. The real
    class ivar table supplies instance offsets; NSException offsets are never
    guessed. No runtime compiler or host TLS operation runs in the guest.
    """
    ISA_CLASS_MASK = 0x0000000ffffffff8
    CLASS_DATA_MASK = 0x00007ffffffffff8
    RW_REALIZED = 1 << 31
    MAX_STRING = 16384

    def __init__(self, process):
        self.process = process

    def read(self, address, size):
        if not address or size < 0 or size > self.MAX_STRING*2:
            raise ValueError('Invalid bounded object read')
        error = lldb.SBError()
        data = self.process.ReadMemory(address, size, error)
        if error.Fail() or len(data) != size:
            raise ValueError('Guest memory unavailable at ' + hex(address))
        return data

    def u64(self, address):
        return struct.unpack('<Q', self.read(address, 8))[0]

    def u32(self, address):
        return struct.unpack('<I', self.read(address, 4))[0]

    def cstring(self, address):
        error = lldb.SBError()
        value = self.process.ReadCStringFromMemory(address, self.MAX_STRING, error)
        if error.Fail():
            raise ValueError('Guest C string unavailable at ' + hex(address))
        return value

    def object_class(self, address):
        isa = self.u64(address)
        return isa & self.ISA_CLASS_MASK if isa & 1 else isa

    def class_ro(self, cls):
        data = self.u64(cls+32) & self.CLASS_DATA_MASK
        return self.u64(data+8) if self.u32(data) & self.RW_REALIZED else data

    def class_name(self, address):
        return self.cstring(self.u64(self.class_ro(self.object_class(address))+24))

    def cfstring(self, address):
        name = self.class_name(address)
        if name not in ('__NSCFString', '__NSCFConstantString', 'NSCFString'):
            raise ValueError('Unsupported string representation: ' + name)
        flags = self.read(address+8, 1)[0]
        inline = flags & 0x60 == 0
        explicit = flags & 5 != 4
        contents = address+16+(8 if explicit else 0) if inline else self.u64(address+16)
        length = self.u64(address+(16 if inline else 24)) if explicit else self.read(contents, 1)[0]
        if length > self.MAX_STRING:
            raise ValueError('String exceeds diagnostic bound')
        contents += 1 if flags & 4 else 0
        data = self.read(contents, length*(2 if flags & 0x10 else 1))
        return data.decode('utf-16-le' if flags & 0x10 else 'ascii')

    def exception(self, address):
        result = {'object': hex(address), 'class': self.class_name(address), 'ivars': {}}
        cls, seen = self.object_class(address), set()
        while cls and cls not in seen and len(seen) < 16:
            seen.add(cls)
            ro = self.class_ro(cls)
            instance_size, table = self.u32(ro+8), self.u64(ro+48)
            if table:
                entry_size, count = self.u32(table) & 0xffff, self.u32(table+4)
                if entry_size != 32 or count > 256:
                    raise ValueError('Unsupported ivar metadata layout')
                for index in range(count):
                    entry = table+8+index*entry_size
                    name = self.cstring(self.u64(entry+8))
                    if name not in ('_name', '_reason', 'name', 'reason'):
                        continue
                    offset = self.u32(self.u64(entry))
                    if offset+8 > instance_size:
                        raise ValueError('Ivar outside declared instance size')
                    value = self.u64(address+offset)
                    record = {'offset': offset, 'value': hex(value)}
                    try:
                        record['text'] = self.cfstring(value) if value else None
                    except (ValueError, UnicodeError) as error:
                        record['decodeError'] = str(error)
                    result['ivars'][name] = record
            cls = self.u64(cls+8)
        return result


def capture_exception(process, breakpoint):
    if breakpoint is None:
        return
    thread = process.GetSelectedThread()
    if thread.GetStopReason() != lldb.eStopReasonBreakpoint or thread.GetStopReasonDataAtIndex(0) != breakpoint.GetID():
        return
    address = thread.GetFrameAtIndex(0).FindRegister('x0').GetValueAsUnsigned()
    print('OBJC_EXCEPTION_OBJECT', hex(address), flush=True)
    try:
        print('OBJC_EXCEPTION ' + json.dumps(GuestObjects(process).exception(address)), flush=True)
    except (ValueError, UnicodeError) as error:
        print('OBJC_EXCEPTION_DECODE_ERROR', str(error), flush=True)
        process.GetTarget().GetDebugger().HandleCommand('memory read --format x --size 8 --count 8 ' + hex(address))
    # Capture actual throwing callers rather than a previous failure's function.
    # These addresses are already mapped guest frames, never guessed cache offsets.
    target = process.GetTarget()
    for symbol in diagnostic_symbols():
        if symbol.get('symbol') == '____getHIDEventSystemClient_block_invoke':
            start = int(symbol['address'], 16)
            print('HID_CLIENT_CODE', hex(start), flush=True)
            target.GetDebugger().HandleCommand('disassemble --start-address ' + hex(start) + ' --count 128')
            capture_code_references(target, start, 128, 'HID_CLIENT_REFERENCE')
            instructions = target.ReadInstructions(target.ResolveLoadAddress(start), 32)
            for instruction in instructions:
                if instruction.GetMnemonic(target) != 'bl':
                    continue
                operand = instruction.GetOperands(target).split()[0]
                if not re.fullmatch('0x[0-9a-fA-F]+', operand):
                    continue
                callee = int(operand, 16)
                print('HID_CLIENT_CALLEE_CODE', hex(callee), flush=True)
                target.GetDebugger().HandleCommand('disassemble --start-address ' + hex(callee) + ' --count 96')
                capture_code_references(target, callee, 96, 'HID_CLIENT_CALLEE_REFERENCE')
    for index in range(1, min(thread.GetNumFrames(), 5)):
        caller = thread.GetFrameAtIndex(index).GetPC()
        if not 0x180000000 <= caller < 0x200000000:
            continue
        start = caller - 96
        print('EXCEPTION_CALLER_CODE', index, hex(caller), flush=True)
        target.GetDebugger().HandleCommand('disassemble --start-address ' + hex(start) + ' --count 64')
        capture_code_references(target, start, 64, 'EXCEPTION_CALLER_REFERENCE')


def resume_bounded(process, seconds=12):
    debugger = process.GetTarget().GetDebugger()
    asynchronous = debugger.GetAsync()
    listener = lldb.SBListener('darwin-journey-resume')
    listener.StartListeningForEvents(process.GetBroadcaster(), lldb.SBProcess.eBroadcastBitStateChanged)
    debugger.SetAsync(True)
    try:
        error = process.Continue()
        if error.Fail():
            raise RuntimeError(str(error))
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline:
            event = lldb.SBEvent()
            if not listener.WaitForEvent(1, event):
                continue
            state = lldb.SBProcess.GetStateFromEvent(event)
            if state == lldb.eStateStopped and lldb.SBProcess.GetRestartedFromEvent(event):
                continue
            if state in (lldb.eStateStopped, lldb.eStateExited, lldb.eStateCrashed, lldb.eStateDetached):
                return
        print('DIAGNOSTIC_WAIT_INTERRUPTED after', seconds, 'seconds; this is not normal app exit', flush=True)
        error = process.Stop()
        if error.Fail():
            raise RuntimeError(str(error))
        deadline = time.monotonic() + 3
        while process.GetState() != lldb.eStateStopped and time.monotonic() < deadline:
            time.sleep(0.05)
        if process.GetState() != lldb.eStateStopped:
            raise RuntimeError('Unable to interrupt the actual waiting process')
    finally:
        listener.StopListeningForEvents(process.GetBroadcaster(), lldb.SBProcess.eBroadcastBitStateChanged)
        debugger.SetAsync(asynchronous)


def capture_ipc_waits(process):
    """Read saved Mach arguments and actual queues; never run guest methods."""
    target = process.GetTarget()
    endpoints = target.FindFirstGlobalVariable('ipc_endpoints')
    queues = {}
    if endpoints.IsValid():
        fields = ('name', 'kind', 'read_fd', 'packet_fd', 'timer_fd', 'queued',
                  'send_refs', 'dead_refs', 'context', 'special_tid', 'sync_destination', 'timer_deadline')
        for index in range(min(endpoints.GetNumChildren(), 128)):
            endpoint = endpoints.GetChildAtIndex(index)
            if endpoint.GetChildMemberWithName('used').GetValueAsUnsigned() != 2:
                continue
            record = {}
            for field in fields:
                value = endpoint.GetChildMemberWithName(field)
                if not value.IsValid() or value.GetError().Fail():
                    record[field] = None
                else:
                    record[field] = value.GetValueAsSigned() if field.endswith('_fd') else value.GetValueAsUnsigned()
            queues[record['name']] = record
    objects = GuestObjects(process)
    backend = target.FindFirstGlobalVariable('darling_active')
    darling = backend.IsValid() and backend.GetValueAsUnsigned() != 0
    for thread in process:
        for frame in thread:
            function = frame.GetFunctionName() or ''
            if darling and 'dserver_rpc_explicit_' in function:
                record = {'tid': thread.GetThreadID(), 'function': function,
                          'framePC': hex(frame.GetPC())}
                call = frame.FindVariable('call')
                for field in ('header', 'body'):
                    value = call.GetChildMemberWithName(field)
                    members = {}
                    if value.IsValid() and value.GetError().Success():
                        for index in range(min(value.GetNumChildren(), 12)):
                            member = value.GetChildAtIndex(index)
                            members[member.GetName()] = member.GetValue() if member.GetError().Success() else None
                    record[field] = members or None
                print('DARLING_RPC_WAIT ' + json.dumps(record), flush=True)
                break
            if 'ipc_message' not in (frame.GetFunctionName() or ''):
                continue
            record = {'tid': thread.GetThreadID(), 'framePC': hex(frame.GetPC())}
            try:
                pointer = frame.FindVariable('r')
                if not pointer.IsValid() or pointer.GetError().Fail() or not pointer.GetValue():
                    raise ValueError('Saved Mach arguments unavailable in this optimized frame')
                registers = struct.unpack('<7Q', objects.read(pointer.GetValueAsUnsigned(), 56))
                record.update({'buffer': hex(registers[0]), 'options': hex(registers[1]),
                               'sendBytes': registers[2], 'receiveBytes': registers[3],
                               'receiveName': registers[4], 'timeoutMilliseconds': registers[5],
                               'notify': registers[6], 'queue': queues.get(registers[4])})
                try:
                    header = struct.unpack('<5Ii', objects.read(registers[0], 24))
                    record['bufferHeader'] = dict(zip(('bits', 'size', 'remote', 'local', 'voucher', 'id'), header))
                except (ValueError, SystemError) as error:
                    record['headerReadError'] = str(error)
                print('IPC_WAIT ' + json.dumps(record), flush=True)
            except (ValueError, SystemError) as error:
                record['error'] = str(error)
                print('IPC_WAIT_DECODE_ERROR ' + json.dumps(record), flush=True)
            break
    if not darling:
        print('IPC_QUEUE_SNAPSHOT ' + json.dumps(list(queues.values())), flush=True)


def capture_normal_fault(target):
    """The ordinary attempt and LLDB retry can fail on different threads."""
    path = Path('build/dyld/app.runtime.log')
    if not path.exists():
        path = Path('build/dyld/runtime-1.log')
    if not path.exists():
        return
    lines = path.read_text(errors='replace').splitlines()
    addresses = set()
    for index, line in enumerate(lines[:-1]):
        if 'DARWIN_NATIVE_FAULT signal/pc/address follows' in line:
            match = re.search(r'pc=(0x[0-9a-fA-F]+)', lines[index+1])
            if match:
                addresses.add(int(match[1], 16))
    for address in sorted(addresses):
        print('NORMAL_RUNTIME_FAULT_PC', hex(address), flush=True)
        capture_code_references(target, max(0, address-128), 40, 'FAULT_REFERENCE')
        target.GetDebugger().HandleCommand('disassemble --start-address ' + hex(max(0, address-128)) + ' --count 64')
