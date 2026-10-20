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
    ranges=[(int(b,16),int(e,16),p) for b,e,p in re.findall(
        r'^([0-9a-f]+)-([0-9a-f]+) ([-rwxps]{4}) ',Path(runtime_path).read_text(),re.M)]

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
                if '/build/apps/' in name:
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
        if images>128:raise ValueError('non-cache image count outside diagnostic bound')
        print('DYLD_COMPACT_METADATA',json.dumps(dict(flags=flags,platform=platform,
              initialCount=initial_count,state=state,images=images)))
        address=0
        for _ in range(images):
            image=mapped_file();address+=image.pop('delta')*page
            print('DYLD_COMPACT_IMAGE',json.dumps(dict(address=hex(address),**image)))
    except (ValueError,struct.error) as error:
        print('DYLD_METADATA_UNAVAILABLE',str(error))
