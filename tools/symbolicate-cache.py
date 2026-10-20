#!/usr/bin/env python3
"""Runner-only symbolication of actual LLDB frames against the real cache.

Keeps textual address/image/nearest-symbol evidence; never exports Apple code.
Format reference: Apple dyld-655.1 dyld_cache_format.h. The nearest available
symbol is diagnostic context, not proof that an unavailable local symbol has
that function name. This is called inside the sole build workflow.
"""
import argparse
import json
import mmap
from pathlib import Path
import re
import struct
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('cache', type=Path)
parser.add_argument('trace', type=Path)
parser.add_argument('output', type=Path)
parser.add_argument('--lookup', action='append', nargs=2, default=[], metavar=('IMAGE', 'SYMBOL'))
args = parser.parse_args()
cache_path, log_path, output_path = args.cache, args.trace, args.output
trace = log_path.read_text()
frames = sorted({int(item, 16) for item in
                 re.findall(r'frame #\d+: (0x[0-9a-fA-F]+)', trace) +
                 re.findall(r'DISPLAY_(?:CONFIGURATION|QUERY_CALLEE)_CODE (0x[0-9a-fA-F]+)', trace) +
                 re.findall(r'DISPLAY_METHOD(?:_CALLEE)?_CODE (0x[0-9a-fA-F]+)', trace) +
                 re.findall(r'HID_CLIENT(?:_CALLEE)?_CODE (0x[0-9a-fA-F]+)', trace) +
                 re.findall(r'NORMAL_RUNTIME_FAULT_PC (0x[0-9a-fA-F]+)', trace)})

with cache_path.open('rb') as stream, mmap.mmap(stream.fileno(), 0, access=mmap.ACCESS_READ) as data:
    def bounded(offset, size):
        if offset < 0 or size < 0 or offset+size > len(data):
            raise ValueError('Cache metadata outside file')
        return offset

    def unpack(fmt, offset):
        return struct.unpack_from(fmt, data, bounded(offset, struct.calcsize(fmt)))

    def string(offset, maximum=8192):
        bounded(offset, 1)
        end = data.find(b'\0', offset, min(offset+maximum, len(data)))
        if end < 0:
            raise ValueError('Unterminated cache string')
        return data[offset:end].decode('utf-8', errors='replace')

    mapping_offset, mapping_count, images_offset, images_count = unpack('<IIII', 16)
    mappings = [unpack('<QQQII', mapping_offset+32*i) for i in range(mapping_count)]

    def file_offset(address):
        for base, size, offset, *_ in mappings:
            if base <= address < base+size:
                return bounded(offset+address-base, 1)
        raise ValueError('Image header outside cache mappings')

    locals_offset, locals_size = unpack('<QQ', 72)
    local_tables = {}
    if locals_size:
        bounded(locals_offset, locals_size)
        noff, ncount, soff, ssize, eoff, ecount = unpack('<IIIIII', locals_offset)
        if noff+ncount*16 > locals_size or soff+ssize > locals_size or eoff+ecount*12 > locals_size:
            raise ValueError('Invalid cache local symbol bounds')
        for i in range(ecount):
            image_offset, first, count = unpack('<III', locals_offset+eoff+i*12)
            if first+count > ncount:
                raise ValueError('Invalid cache local symbol indexes')
            local_tables[image_offset] = (locals_offset+noff+first*16, count, locals_offset+soff, ssize)

    records = []
    seen = set()
    for i in range(images_count):
        address, _, _, path_offset, _ = unpack('<QQQII', images_offset+i*32)
        if address in seen:
            continue
        seen.add(address)
        start = file_offset(address)
        magic, cpu, subtype, kind, count, command_bytes, flags, _ = unpack('<IIIIIIII', start)
        if magic != 0xfeedfacf or cpu != 0x100000c or kind != 6:
            raise ValueError('Unsupported cache image header')
        cursor, end = start+32, start+32+command_bytes
        executable = []
        tables = []
        for _ in range(count):
            command, size = unpack('<II', cursor)
            if size < 8 or cursor+size > end:
                raise ValueError('Invalid cache load command')
            if command == 0x19:
                base, length = unpack('<QQ', cursor+24)
                protection, = unpack('<I', cursor+60)
                if protection & 4:
                    executable.append((base, length))
            elif command == 2:
                offset, number, strings, length = unpack('<IIII', cursor+8)
                tables.append((offset, number, strings, length))
            cursor += size
        if cursor != end:
            raise ValueError('Invalid cache command extent')
        matches = [pc for pc in frames if any(base <= pc < base+length for base, length in executable)]
        image_path = string(path_offset)
        requested = {name for image, name in args.lookup if image == image_path}
        if not matches and not requested:
            continue
        if start in local_tables:
            tables.append(local_tables[start])
        symbols = []
        for offset, number, strings, string_bytes in tables:
            bounded(offset, number*16)
            bounded(strings, string_bytes)
            for n in range(number):
                index, stype, section, desc, value = unpack('<IBBHQ', offset+n*16)
                if stype & 0xe0 or stype & 0x0e != 0x0e or not section or index >= string_bytes:
                    continue
                name = string(strings+index, min(8192, string_bytes-index))
                if name in requested:
                    record = {'image': image_path, 'symbol': name, 'address': hex(value)}
                    if record not in records:
                        records.append(record)
                        print('CACHE_SYMBOL '+json.dumps(record), flush=True)
                if any(base <= value < base+length for base, length in executable):
                    symbols.append((value, name))
        symbols.sort()
        for pc in matches:
            candidates = [(value, name) for value, name in symbols if value <= pc]
            nearest = max(candidates, default=None)
            record = {'pc': hex(pc), 'image': string(path_offset), 'imageAddress': hex(address)}
            if nearest:
                record.update(nearestSymbol=nearest[1], symbolAddress=hex(nearest[0]), displacement=pc-nearest[0])
            records.append(record)
            print('CACHE_FRAME '+json.dumps(record), flush=True)
output_path.write_text(json.dumps(records, indent=2)+'\n')
