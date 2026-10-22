"""Read original dyld metadata through LLDB; never execute guest code.

Layout: Apple dyld include/mach-o/dyld_images.h (LP64 version >=16),
common/ProcessAtlas.cpp Serializer version 0 and lsl/PVLEInt64.cpp.
Only mapped non-executable metadata is read; raw bytes are not exported.
"""
import json
from pathlib import Path
import re
import struct
import lldb


def capture_frame(target,directory):
    """One stopped-process source buffer, never per-frame guest-main-loop I/O."""
    try:
        process=target.GetProcess()
        if process.GetState()!=lldb.eStateStopped:raise ValueError('original process is not stopped')
        ui=target.FindFirstGlobalVariable('ui')
        if not ui.IsValid():raise ValueError('original renderer metadata unavailable')
        def field(name):return ui.GetChildMemberWithName(name).GetValueAsUnsigned()
        width,height=field('width'),field('height');size=width*height*4
        if not (0<width<=8192 and 0<height<=8192 and size<=64*1024*1024):
            raise ValueError('source frame outside diagnostic byte budget')
        control=ui.GetChildMemberWithName('control').Dereference()
        state=control.GetChildMemberWithName('state');sequences=control.GetChildMemberWithName('sequence')
        choices=[(sequences.GetChildAtIndex(i).GetValueAsUnsigned(),i) for i in range(2)
                 if state.GetChildAtIndex(i).GetValueAsUnsigned() in (0,2,3)]
        sequence,slot=max(choices,default=(0,0))
        if not sequence:raise ValueError('no completed source buffer; drawing slots excluded')
        address=field('pixels')+slot*size
        region=lldb.SBMemoryRegionInfo();error=process.GetMemoryRegionInfo(address,region)
        if error.Fail() or not region.IsReadable() or region.IsExecutable() or address+size>region.GetRegionEnd():
            raise ValueError('source frame outside readable non-executable mapping')
        output=Path(directory)/f'frame-{process.GetProcessID()}.bgra'
        temporary=output.with_suffix('.bgra.tmp')
        with temporary.open('wb') as stream:
            stream.write(struct.pack('<III',width,height,width*4))
            for offset in range(0,size,131072):
                count=min(131072,size-offset);error=lldb.SBError()
                data=process.ReadMemory(address+offset,count,error)
                if error.Fail() or len(data)!=count:raise ValueError('incomplete source frame read: '+str(error))
                stream.write(data)
        temporary.replace(output)
        print('ORIGINAL_SOURCE_FRAME',json.dumps(dict(pid=process.GetProcessID(),sequence=sequence,slot=slot,
            width=width,height=height,bytes=size,path=str(output),
            note='Completed source buffer; composed journey screenshots remain display acceptance')))
    except (ValueError,OSError,OverflowError) as error:
        print('ORIGINAL_SOURCE_FRAME_UNAVAILABLE',str(error))


def collect(target, info_address, runtime_path):
    owned_images=[]
    process=target.GetProcess()
    maps=Path(runtime_path).read_text()
    if 'DARWIN_FAULT_MAPS_BEGIN' not in maps:
        maps=Path(f'/proc/{process.GetProcessID()}/maps').read_text()
    ranges=[(int(b,16),int(e,16),p) for b,e,p in re.findall(
        r'^([0-9a-f]+)-([0-9a-f]+) ([-rwxps]{4}) ',maps,re.M)]

    def capacity(address):
        return next((end-address for begin,end,permissions in ranges
                     if begin<=address<end and permissions[0]=='r' and permissions[2]!='x'),0)

    def read(address,size):
        if size<0 or size>131072 or size>capacity(address):
            raise ValueError('outside bounded readable metadata mapping')
        error=lldb.SBError()
        result=process.ReadMemory(address,size,error)
        if error.Fail() or len(result)!=size:raise ValueError(str(error))
        return result

    def string(address):
        if not address:return None
        data=read(address,min(1024,capacity(address)))
        if b'\0' not in data:raise ValueError('unbounded metadata string')
        return data.split(b'\0',1)[0].decode('utf-8',errors='replace')

    # Host unwinding can stop inside an optimized SIGSYS adapter. Read its
    # live kernel-owned ucontext using DWARF, then follow only actual readable
    # ARM64 frame records. Never scan for plausible PCs or execute the guest.
    try:
        threads=target.FindFirstGlobalVariable('native_threads')
        if not threads.IsValid():raise ValueError('runtime guest threads unavailable')
        for index in range(threads.GetNumChildren()):
            thread=threads.GetChildAtIndex(index)
            if thread.GetChildMemberWithName('state').GetValueAsUnsigned()!=2:continue
            context=thread.GetChildMemberWithName('diagnostic_syscall_context')
            if not context.GetValueAsUnsigned():continue
            machine=context.Dereference().GetChildMemberWithName('uc_mcontext')
            if not machine.IsValid() or machine.GetError().Fail():raise ValueError('original syscall context unreadable')
            tid=thread.GetChildMemberWithName('tid').GetValueAsUnsigned()
            registers=machine.GetChildMemberWithName('regs')
            pc=machine.GetChildMemberWithName('pc').GetValueAsUnsigned()
            frame=registers.GetChildAtIndex(29).GetValueAsUnsigned()
            lr=registers.GetChildAtIndex(30).GetValueAsUnsigned()
            syscall=registers.GetChildAtIndex(16).GetValueAsUnsigned()
            print(f'DARWIN_SYSCALL_CONTEXT pc={pc:#x} tid={tid} syscall={syscall:#x}')
            print(f'DARWIN_SYSCALL_FRAME nr=0x0 pc={lr:#x} tid={tid}')
            for depth in range(1,25):
                if not frame or frame%16:break
                try:parent,returned=struct.unpack('<QQ',read(frame,16))
                except ValueError as error:
                    print('DARWIN_SYSCALL_FRAME_UNAVAILABLE',tid,str(error));break
                print(f'DARWIN_SYSCALL_FRAME nr={depth:#x} pc={returned:#x} tid={tid}')
                if parent<=frame:break
                frame=parent
            else:print('DARWIN_SYSCALL_FRAME_TRUNCATED',tid,'limit=24')
    except (ValueError,OverflowError) as error:
        print('DARWIN_SYSCALL_CONTEXT_UNAVAILABLE',str(error))

    # Read runtime-owned workloop state through its real DWARF types, then the
    # guest queue label/serial offsets actually registered by libdispatch. No
    # private app method, guessed object layout or synthetic queue is invoked.
    try:
        config=target.FindFirstGlobalVariable('native_dispatch_config')
        queue=target.FindFirstGlobalVariable('native_kqueues')
        if not config.IsValid() or not queue.IsValid():raise ValueError('runtime workloop symbols unavailable')
        serial_offset=config.GetChildMemberWithName('serial_offset').GetValueAsUnsigned()
        label_offset=config.GetChildMemberWithName('label_offset').GetValueAsUnsigned()
        seen=set()
        while queue.GetValueAsUnsigned() and len(seen)<16:
            address=queue.GetValueAsUnsigned()
            if address in seen:raise ValueError('cycle in original runtime workloop list')
            seen.add(address);node=queue.Dereference()
            if node.GetError().Fail():raise ValueError(str(node.GetError()))
            fields={name:node.GetChildMemberWithName(name).GetValueAsUnsigned() for name in
                    ('dynamic_id','worker','owner_tid','client_owner','priority','used')}
            guest=fields['dynamic_id']
            if guest and 0<serial_offset<=1024:
                try:fields['guestSerial']=struct.unpack('<Q',read(guest+serial_offset,8))[0]
                except ValueError as error:fields['serialUnavailable']=str(error)
            if guest and 0<label_offset<=1024:
                try:fields['guestLabel']=(string(struct.unpack('<Q',read(guest+label_offset,8))[0]) or '')[:256]
                except ValueError as error:fields['labelUnavailable']=str(error)
            print('ORIGINAL_WORKLOOP_METADATA',json.dumps(fields))
            queue=node.GetChildMemberWithName('next')
        if queue.GetValueAsUnsigned():print('ORIGINAL_WORKLOOP_METADATA_TRUNCATED limit=16')
    except (ValueError,OverflowError) as error:
        print('ORIGINAL_WORKLOOP_METADATA_UNAVAILABLE',str(error))

    try:
        # LP64 offsets from the declared fields, including the notify-port and
        # reserved arrays. No Apple instructions or Mach-O headers are read.
        info=read(info_address,0x140)
        version,count,array=struct.unpack_from('<IIQ',info)
        if not 16<=version<=32 or count>4096:raise ValueError('unsupported image-info layout')
        compact_address,compact_size=struct.unpack_from('<QQ',info,0x130)
        print('DYLD_IMAGE_METADATA',json.dumps(dict(version=version,count=count,
              compactAddress=hex(compact_address),compactSize=compact_size)))
        if array:
            entries=read(array,count*24)
            for offset in range(0,len(entries),24):
                address,path,_=struct.unpack_from('<QQQ',entries,offset)
                if not capacity(path):continue
                name=string(path)
                if '/build/apps/' in name or '/build/ios-input/apps/' in name:
                    owned_images.append(dict(address=address,path=name))
                    print('DYLD_OWNED_IMAGE',json.dumps(dict(address=hex(address),path=name)))
        if not 36<=compact_size<=32768:raise ValueError('compact metadata size outside bound')
        data=read(compact_address,compact_size)
        magic,version=struct.unpack_from('<II',data)
        if magic!=0xa71a5166 or version:raise ValueError('unsupported compact metadata format')
        cursor=36

        def take(size):
            nonlocal cursor
            if size<0 or cursor+size>len(data):raise ValueError('compact metadata overrun')
            result=data[cursor:cursor+size];cursor+=size
            return result

        def pvle():
            first=take(1)[0]
            if not first:return int.from_bytes(take(8),'little')
            size=(first&-first).bit_length()
            return int.from_bytes(bytes([first])+take(size-1),'little')>>size

        flags,platform,initial_count,state,volumes=[pvle() for _ in range(5)]
        if flags&~7 or volumes>4096:raise ValueError('unsupported compact flags/volume count')
        take(volumes*16)
        strings=take(pvle())
        page=16384 if flags&4 else 4096

        def mapped_file():
            fields,delta=pvle(),pvle()
            if fields&~7:raise ValueError('unsupported mapped-file flags')
            uuid=take(16).hex() if fields&4 else None
            file_id=[pvle(),pvle()] if fields&1 else None
            path=None
            if fields&2:
                offset=pvle()
                if offset>=len(strings) or b'\0' not in strings[offset:]:raise ValueError('invalid string-table offset')
                path=strings[offset:].split(b'\0',1)[0].decode('utf-8',errors='replace')
            return dict(delta=delta,uuid=uuid,fileID=file_id,path=path)

        if flags&1:
            cache=mapped_file();bits=pvle();take((bits+7)//8)
            print('DYLD_COMPACT_CACHE',json.dumps(dict(address=hex(cache['delta']*page),bitmapBits=bits)))
        images=pvle()
        if images>4096:raise ValueError('non-cache image count outside diagnostic bound')
        print('DYLD_COMPACT_METADATA',json.dumps(dict(flags=flags,platform=platform,
              initialCount=initial_count,state=state,images=images)))
        address=0
        for _ in range(images):
            image=mapped_file();address+=image.pop('delta')*page
            if '/build/apps/' in (image['path'] or '') or image['path']=='/usr/lib/dyld':
                print('DYLD_COMPACT_IMAGE',json.dumps(dict(address=hex(address),**image)))
    except (ValueError,struct.error) as error:
        print('DYLD_METADATA_UNAVAILABLE',str(error))
    return owned_images
