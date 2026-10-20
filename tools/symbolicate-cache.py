#!/usr/bin/env python3
"""Symbolicate original-process frames using UUID-matched actual cache members.
Nearest symbols are context, never proof of an exact function match.
"""
import argparse
import json
from pathlib import Path
import re
from cache_symbols import CacheSymbols

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('cache',type=Path)
parser.add_argument('trace',type=Path)
parser.add_argument('output',type=Path)
parser.add_argument('--lookup',action='append',nargs=2,default=[],metavar=('IMAGE','SYMBOL'))
args=parser.parse_args()
trace=args.trace.read_text(errors='replace')
patterns=[r'frame #\d+: (0x[0-9a-fA-F]+)',r'DARWIN_EXIT_FRAME nr=\S+ pc=(0x[0-9a-fA-F]+)',
          r'NORMAL_RUNTIME_FAULT_PC (0x[0-9a-fA-F]+)',
          r'DARWIN_NATIVE_FAULT[^\n]*\nDARWIN_CALL nr=\S+ pc=(0x[0-9a-fA-F]+)',
          r'DARWIN_FAULT signal=\S+ pc=(0x[0-9a-fA-F]+)']
frames=sorted({int(pc,16) for pattern in patterns for pc in re.findall(pattern,trace)})
records=[];matched=set()
with CacheSymbols(args.cache) as cache:
    for image in cache.images():
        ranges=image['executable']
        matches=[pc for pc in frames if any(base<=pc<base+size for base,size in ranges)]
        requested={name for path,name in args.lookup if path==image['name']}
        if not matches and not requested:continue
        symbols=[]
        for value,name in cache.symbols(image):
            if name in requested:
                record={'image':image['name'],'symbol':name,'address':hex(value)}
                if record not in records:records.append(record)
            if any(base<=value<base+size for base,size in ranges):symbols.append((value,name))
        symbols.sort()
        for pc in matches:
            matched.add(pc)
            nearest=max(((value,name) for value,name in symbols if value<=pc),default=None)
            record={'pc':hex(pc),'image':image['name'],'imageAddress':hex(image['address'])}
            if nearest:record.update(nearestSymbol=nearest[1],symbolAddress=hex(nearest[0]),displacement=pc-nearest[0])
            records.append(record)
    for pc in frames:
        if pc not in matched:records.append({'pc':hex(pc),'status':'outside-cache','note':'Requires original app/interpreter or host symbols'})
args.output.write_text(json.dumps(records,indent=2)+'\n')
for record in records:print('CACHE_SYMBOLICATION '+json.dumps(record),flush=True)
