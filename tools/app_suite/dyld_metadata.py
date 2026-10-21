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


def collect(target, info_address, runtime_path):
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
        data=read(address,min(1024,capacity(address)))
        if b'\0' not in data:raise ValueError('unbounded metadata string')
        return data.split(b'\0',1)[0].decode('utf-8',errors='replace')

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
                try:fields['guestLabel']=string(struct.unpack('<Q',read(guest+label_offset,8))[0])[:256]
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
