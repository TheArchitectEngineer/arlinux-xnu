"""Bounded debugger observations of real original-app entry/callback execution.

Only the opt-in diagnostic profile uses this. Breakpoint addresses come from
the actual Mach-O symbol table and dyld image metadata, never firmware offsets.
No expression evaluation, private action invocation or return-value changes.
"""
import json
from pathlib import Path
import re
import runpy
import struct
import subprocess
import time


def objc_method_address(data,class_name,selector):
    """Resolve an original instance method from its own Mach-O metadata.

    This is source inspection, not a guest Objective-C message or runtime patch.
    Supports LP64 class_ro and ordinary/relative method lists in original IPAs.
    """
    segments=[];classlists=[];text=None;cursor=32
    commands,command_bytes=struct.unpack_from('<II',data,16)
    end=32+command_bytes
    if end>len(data) or commands>16384:raise ValueError('invalid original load commands')
    for _ in range(commands):
        command,size=struct.unpack_from('<II',data,cursor)
        if size<8 or cursor+size>end:raise ValueError('invalid original command bounds')
        if command==0x19:
            if size<72:raise ValueError('invalid original segment')
            name,address,length,offset,file_bytes,_,protection,count,_=struct.unpack_from('<16sQQQQiiII',data,cursor+8)
            if offset+file_bytes>len(data) or count>(size-72)//80:raise ValueError('invalid original segment bounds')
            segments.append((address,file_bytes,offset,protection))
            if name.rstrip(b'\0')==b'__TEXT':text=address
            for n in range(count):
                name,_,address,length,offset=struct.unpack_from('<16s16sQQI',data,cursor+72+n*80)
                if name.rstrip(b'\0')==b'__objc_classlist':
                    if length%8 or offset+length>len(data) or length>1024*1024:raise ValueError('invalid original class list')
                    classlists.append((offset,length))
        cursor+=size
    def position(address,size):
        for base,length,offset,_ in segments:
            if base<=address and size<=length-(address-base):return offset+address-base
        raise ValueError('original Objective-C metadata outside file-backed segments')
    def pointer(address):return struct.unpack_from('<Q',data,position(address,8))[0]
    def string(address):
        offset=position(address,1);limit=min(len(data),offset+1024)
        end=data.find(b'\0',offset,limit)
        if end<0:raise ValueError('unbounded original Objective-C name')
        position(address,end-offset+1)
        return data[offset:end].decode('utf-8')
    for offset,length in classlists:
        for n in range(length//8):
            cls=struct.unpack_from('<Q',data,offset+n*8)[0]
            ro=pointer(cls+32)&~7
            if string(pointer(ro+24))!=class_name:continue
            methods=pointer(ro+32)
            if not methods:raise ValueError('original class has no instance method list')
            flags,count=struct.unpack_from('<II',data,position(methods,8));stride=flags&0xffff
            relative=bool(flags&0x80000000)
            if stride<(12 if relative else 24) or count>16384:raise ValueError('invalid original method list')
            position(methods+8,count*stride)
            for index in range(count):
                entry=methods+8+index*stride
                if relative:
                    name,_,implementation=struct.unpack_from('<iii',data,position(entry,12))
                    name=entry+name
                    if not flags&0x40000000:name=pointer(name)
                    implementation=entry+8+implementation
                else:name,_,implementation=struct.unpack_from('<QQQ',data,position(entry,24))
                if string(name)!=selector:continue
                if text is None or implementation%4 or not any(
                        base<=implementation and implementation+4<=base+size and protection&4
                        for base,size,_,protection in segments):
                    raise ValueError('original method is outside executable file-backed code')
                return implementation-text
            raise ValueError('declared original instance method not found')
    raise ValueError('declared original Objective-C class not found')


def symbol_address(path,name,file_offset=False):
    data=Path(path).read_bytes()
    magic,_,_,_,count,size,_,_=struct.unpack_from('<8I',data)
    if magic!=0xfeedfacf or count>16384 or size>16*1024*1024:
        raise ValueError('unsupported original Mach-O metadata')
    offset=32;base=None;text_file=None
    for _ in range(count):
        command,length=struct.unpack_from('<II',data,offset)
        if length<8 or offset+length>32+size:raise ValueError('invalid Mach-O commands')
        if command==0x19 and data[offset+8:offset+24].rstrip(b'\0')==b'__TEXT':
            base=struct.unpack_from('<Q',data,offset+24)[0]
            text_file=struct.unpack_from('<Q',data,offset+40)[0]
        offset+=length
    if base is None:raise ValueError('original text segment unavailable')
    method=re.fullmatch(r'-\[([^ ]+) ([^\]]+)\]',name)
    if method:
        address=objc_method_address(data,*method.groups())
        return address+(text_file if file_offset else 0)
    # LLVM already handles both nlist and stripped Mach-O export tries. Use
    # the runner's existing pinned toolchain instead of another format reader.
    symbols=subprocess.check_output(['llvm-nm-18','--defined-only','--format=posix',str(path)],text=True,timeout=3)
    for line in symbols.splitlines():
        fields=line.split()
        if len(fields)>=3 and fields[0]==name and fields[1].upper()=='T':
            return int(fields[2],16)-base+(text_file if file_offset else 0)
    raise ValueError('declared original symbol not found: '+name)


def trace(debugger,specification,runtime_path,stderr_path):
    import lldb
    target=debugger.GetSelectedTarget();process=target.GetProcess()
    deadline=time.monotonic()+20
    debugger.SetAsync(True)
    listener=lldb.SBListener('original-app-trace')
    process.GetBroadcaster().AddListener(listener,lldb.SBProcess.eBroadcastBitStateChanged)
    signals=process.GetUnixSignals()
    for name in ('SIGSYS','SIGSTOP'):
        number=signals.GetSignalNumberFromName(name)
        signals.SetShouldStop(number,False);signals.SetShouldNotify(number,False)
    breakpoints={};deferred=[];return_mismatches=0
    def arm(record):
        point=target.BreakpointCreateByAddress(int(record['address'],16))
        if record.get('thread'):point.SetThreadID(record['thread'])
        breakpoints[point.GetID()]=record
        print('ORIGINAL_TRACE_ARMED',json.dumps(record))
    try:
        entry=re.search(r'NATIVE_DARWIN_ENTRY entry=(0x[0-9a-f]+)',Path(stderr_path).read_text(errors='replace'))
        if not entry:raise ValueError('original entry address unavailable')
        first=target.BreakpointCreateByAddress(int(entry[1],16))
        breakpoints[first.GetID()]=dict(stage='original-main')
        process.Continue()
        while time.monotonic()<deadline:
            event=lldb.SBEvent()
            if not listener.WaitForEvent(1,event):continue
            state=lldb.SBProcess.GetStateFromEvent(event)
            if state in (lldb.eStateExited,lldb.eStateDetached,lldb.eStateInvalid):break
            if state!=lldb.eStateStopped or lldb.SBProcess.GetRestartedFromEvent(event):continue
            stopped=[]
            for thread in process:
                if thread.GetStopReason()!=lldb.eStopReasonBreakpoint:continue
                key=thread.GetStopReasonDataAtIndex(0)
                record=breakpoints.get(key)
                if record is not None:
                    # A recursive caller can reuse the same link register on
                    # this thread. Only the entry stack pointer identifies the
                    # invocation whose return we are observing (AAPCS64).
                    if record.get('stackPointer') and thread.GetFrameAtIndex(0).GetSP()!=int(record['stackPointer'],16):
                        return_mismatches+=1
                        if return_mismatches<=4:
                            print('ORIGINAL_TRACE_RETURN_OTHER_INVOCATION',json.dumps(dict(
                                symbol=record['symbol'],tid=thread.GetThreadID(),
                                expectedSP=record['stackPointer'],actualSP=hex(thread.GetFrameAtIndex(0).GetSP()))))
                        stopped.append((thread,key,None))
                        continue
                    breakpoints.pop(key)
                    stopped.append((thread,key,record))
            if not stopped:
                print('ORIGINAL_TRACE_UNEXPECTED_STOP',process.GetState())
                break
            for thread,key,record in stopped:
                if record is None:continue
                target.BreakpointDelete(key)
                print('ORIGINAL_TRACE_HIT',json.dumps(dict(tid=thread.GetThreadID(),**record)))
                if record['stage']=='original-main':
                    info=re.findall(r'DARLING_DYLD_INFO bytes=\S+ pc=(0x[0-9a-f]+) result=0x0+ ',Path(runtime_path).read_text(errors='replace'))
                    if not info:raise ValueError('original dyld image metadata unavailable')
                    images=runpy.run_path(str(Path(__file__).with_name('dyld_metadata.py')))['collect'](target,int(info[-1],16),runtime_path)
                    for requested in json.loads(Path(specification).read_text()):
                        image=next((image for image in images if image['path'].endswith('/'+requested['image'])),None)
                        if image is None:raise ValueError('declared original image not loaded')
                        address=image['address']+symbol_address(image['path'],requested['symbol'])
                        record=dict(stage='symbol',address=hex(address),**requested)
                        if requested.get('afterSymbol'):deferred.append(record)
                        else:arm(record)
                else:
                    frame=thread.GetFrameAtIndex(0)
                    print('ORIGINAL_TRACE_STACK',thread.GetThreadID())
                    for index in range(min(16,thread.GetNumFrames())):
                        print(f'frame #{index}: {thread.GetFrameAtIndex(index).GetPC():#x}')
                    if record['stage']=='symbol':
                        for waiting in deferred[:]:
                            if waiting['afterSymbol']==record['symbol']:
                                arm(waiting);deferred.remove(waiting)
                        if record.get('observeReturn'):
                            address=frame.FindRegister('x30').GetValueAsUnsigned()
                            if not address or address%4:raise ValueError('original return address invalid')
                            stack=frame.GetSP()
                            if not stack or stack%16:raise ValueError('original entry stack pointer invalid')
                            arm(dict(stage='return',address=hex(address),symbol=record['symbol'],thread=thread.GetThreadID(),stackPointer=hex(stack)))
                    if record.get('callbackRegister'):
                        address=frame.FindRegister(record['callbackRegister']).GetValueAsUnsigned()
                        if not address or address%4:raise ValueError('original callback address invalid')
                        point=target.BreakpointCreateByAddress(address)
                        breakpoints[point.GetID()]=dict(stage='callback',address=hex(address),symbol=record['symbol'])
                        print('ORIGINAL_TRACE_ARMED',json.dumps(breakpoints[point.GetID()]))
                    if record.get('callbackBlockRegister'):
                        block=frame.FindRegister(record['callbackBlockRegister']).GetValueAsUnsigned()
                        if not block:
                            print('ORIGINAL_TRACE_BLOCK_ABSENT',record['symbol'])
                        else:
                            # ARM64 Blocks ABI: isa, flags/reserved, invoke,
                            # descriptor. Read the supplied object, never copy,
                            # retain or invoke it. Stack/global blocks are valid.
                            region=lldb.SBMemoryRegionInfo()
                            error=process.GetMemoryRegionInfo(block,region)
                            if error.Fail() or not region.IsReadable() or region.IsExecutable() or block+32>region.GetRegionEnd():
                                raise ValueError('original supplied block is outside readable metadata')
                            error=lldb.SBError();data=process.ReadMemory(block,32,error)
                            if error.Fail() or len(data)!=32:raise ValueError('original block metadata unreadable')
                            address=struct.unpack_from('<Q',data,16)[0]
                            error=process.GetMemoryRegionInfo(address,region)
                            if not address or address%4 or error.Fail() or not region.IsExecutable():
                                raise ValueError('original block invoke is outside executable memory')
                            arm(dict(stage='callback',address=hex(address),symbol=record['symbol'],block=hex(block)))
            process.Continue()
            if not breakpoints:break
        print('ORIGINAL_TRACE_WINDOW_ENDED',json.dumps(dict(unhit=list(breakpoints.values())+deferred,returnInvocationMismatches=return_mismatches)))
    except Exception as error:
        print('ORIGINAL_TRACE_UNAVAILABLE',str(error))
    finally:
        if process.GetState() not in (lldb.eStateExited,lldb.eStateDetached,lldb.eStateInvalid):
            debugger.SetAsync(False)
            process.Stop()
            for key in breakpoints:target.BreakpointDelete(key)
            error=process.Detach()
            print('ORIGINAL_TRACE_DETACH',str(error))
