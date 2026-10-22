#!/usr/bin/env python3
"""Symbolicate original-process frames using UUID-matched actual cache members.
Nearest symbols are context, never proof of an exact function match.
"""
import argparse
import json
from pathlib import Path
import re
from cache_symbols import CacheSymbols,nearest_symbol

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('cache',type=Path)
parser.add_argument('trace',type=Path)
parser.add_argument('output',type=Path)
parser.add_argument('--stderr',type=Path,help='Same original process stderr with opt-in uncaught-exception frames')
parser.add_argument('--lookup',action='append',nargs=2,default=[],metavar=('IMAGE','SYMBOL'))
parser.add_argument('--lookup-selector',action='append',nargs=2,default=[],metavar=('IMAGE','SELECTOR'),
                    help='Resolve bounded actual class/instance selector symbols before reading their code')
parser.add_argument('--lookup-fragment',action='append',nargs=2,default=[],metavar=('IMAGE','FRAGMENT'),
                    help='Resolve at most four actual matching function symbols; ambiguous matches remain metadata only')
parser.add_argument('--lookup-bytes',type=int,help='Override instruction-byte budget per explicitly selected lookup/method (multiple of 4, at most 4096)')
parser.add_argument('--bootstrap-context',action='store_true')
parser.add_argument('--list-symbols',action='append',nargs=2,default=[],metavar=('IMAGE','FRAGMENT'))
parser.add_argument('--objc-method',action='append',nargs=3,default=[],metavar=('IMAGE','CLASS','SELECTOR'))
args=parser.parse_args()
if args.lookup_bytes is not None and (not 4<=args.lookup_bytes<=4096 or args.lookup_bytes%4):
    parser.error('--lookup-bytes must be a multiple of 4 between 4 and 4096')
trace=args.trace.read_text(errors='replace')
if args.stderr:trace+='\n'+args.stderr.read_text(errors='replace')
patterns=[r'frame #\d+: (0x[0-9a-fA-F]+)',r'DARWIN_(?:EXIT|FAULT|UNIMPLEMENTED|MMAP_FAILURE|SYSCALL)_FRAME nr=\S+ pc=(0x[0-9a-fA-F]+)',
          r'DARWIN_SYSCALL_CONTEXT pc=(0x[0-9a-fA-F]+)',
          r'NORMAL_RUNTIME_FAULT_PC (0x[0-9a-fA-F]+)',
          r'DARWIN_NATIVE_FAULT[^\n]*\nDARWIN_CALL nr=\S+ pc=(0x[0-9a-fA-F]+)',
          r'DARWIN_FAULT signal=\S+ pc=(0x[0-9a-fA-F]+)']
frames=sorted({int(pc,16) for pattern in patterns for pc in re.findall(pattern,trace)})
exception_returns={int(pc,16) for pc in re.findall(r'IOS_UNCAUGHT_EXCEPTION_FRAME index=\d+ pc=(0x[0-9a-fA-F]+)',trace)}
exception_returns.update(int(pc,16) for pc in re.findall(r'IOS_FILE_COORDINATION_FRAME index=\d+ pc=(0x[0-9a-fA-F]+)',trace))
frames=sorted(set(frames)|exception_returns)
faults={int(pc,16) for pc in re.findall(r'DARWIN_FAULT signal=\S+ pc=(0x[0-9a-fA-F]+)',trace)}
returns={int(pc,16):int(index,16) for index,pc in re.findall(
    r'DARWIN_(?:EXIT|FAULT|UNIMPLEMENTED|MMAP_FAILURE|SYSCALL)_FRAME nr=0x([0-9a-fA-F]+) pc=(0x[0-9a-fA-F]+)',trace)}
returns.update({pc:0 for pc in exception_returns})
unsupported_returns={int(pc,16) for pc in re.findall(
    r'DARWIN_UNIMPLEMENTED_FRAME nr=\S+ pc=(0x[0-9a-fA-F]+)',trace)}
unsupported_returns.update(exception_returns)
unsupported_returns.update(int(pc,16) for pc in re.findall(
    r'DARWIN_MMAP_FAILURE_FRAME nr=\S+ pc=(0x[0-9a-fA-F]+)',trace))
unsupported_returns.update(int(pc,16) for pc in re.findall(
    r'DARWIN_SYSCALL_FRAME nr=\S+ pc=(0x[0-9a-fA-F]+)',trace))
stack_returns={int(pc,16) for index,pc in re.findall(r'frame #(\d+): (0x[0-9a-fA-F]+)',trace) if int(index)>0}
def lookup_pc(pc):return pc-4 if (pc in returns or pc in stack_returns) and pc not in faults else pc
fault_arguments=[(int(index,16),int(value,16)) for index,value in re.findall(
    r'DARWIN_FAULT_REGISTER index=0x([0-9a-fA-F]+) pc=\S+ result=0x([0-9a-fA-F]+)',trace) if int(index,16)<31]
records=[];matched=set()
with CacheSymbols(args.cache) as cache:
    targets=[]
    for pc in (set(returns)|stack_returns)-unsupported_returns:
        if pc in faults:continue
        try:call=cache.return_call(pc)
        except ValueError:continue # Original app/host addresses are outside this cache.
        if call:
            records.append({'originalCall':call})
            targets.append(int(call['target'],16))
    frames=sorted(set(frames+targets))
    for image in cache.images():
        for register,address in fault_arguments:
            for base,size in image['cstring']:
                if base<=address<base+size:
                    maximum=min(512,base+size-address)
                    member,offset=cache.locate(address,maximum)
                    data=member.data[offset:offset+maximum]
                    records.append({'faultRegister':register,'address':hex(address),'image':image['name'],
                        'literal':data.split(b'\0',1)[0].decode('utf-8',errors='replace'),'truncated':b'\0' not in data,
                        'note':'Original fault argument points into a declared cache C-string section'})
        if args.bootstrap_context and image['name']=='/usr/lib/system/libxpc.dylib':
            records.append({'image':image['name'],'bootstrapCandidates':cache.bootstrap_context(image)})
        ranges=image['executable']
        matches=[pc for pc in frames if any(base<=lookup_pc(pc)<base+size for base,size in ranges)]
        requested={name for path,name in args.lookup if path==image['name']}
        selectors=[selector for path,selector in args.lookup_selector if path==image['name']]
        fragments=[fragment for path,fragment in args.lookup_fragment if path==image['name']]
        queries=[fragment for path,fragment in args.list_symbols if path==image['name']]
        methods=[(cls,selector) for path,cls,selector in args.objc_method if path==image['name']]
        if not matches and not requested and not selectors and not fragments and not queries and not methods:continue
        actual_symbols=list(cache.symbols(image))
        for selector in selectors:
            found=sorted({name for _,name in actual_symbols if name.startswith(('-[','+[')) and name.endswith(' '+selector+']')})
            records.append({'image':image['name'],'selectorLookup':selector,'count':len(found),'matches':found[:4],
                            'status':'not-found' if not found else 'bounded' if len(found)<=4 else 'ambiguous-use-exact-symbol'})
            if len(found)<=4:requested.update(found)
        for fragment in fragments:
            found=sorted({name for _,name in actual_symbols if fragment in name})
            records.append({'image':image['name'],'functionLookup':fragment,'count':len(found),'matches':found[:4],
                'status':'not-found' if not found else 'bounded' if len(found)<=4 else 'ambiguous-use-exact-symbol'})
            if len(found)<=4:requested.update(found)
        for query in queries:
            found=[{'address':hex(value),'symbol':name} for value,name in actual_symbols if query.lower() in name.lower()]
            records.append({'image':image['name'],'symbolsMatching':query,'count':len(found),'truncated':len(found)>256,'matches':found[:256]})
        symbols=[]
        requested_addresses=[]
        for value,name in actual_symbols:
            if name in requested:
                record={'image':image['name'],'symbol':name,'address':hex(value)}
                if record not in records:records.append(record)
                requested_addresses.append((value,name,record))
            if any(base<=value<base+size for base,size in ranges):symbols.append((value,name))
        symbols.sort()
        for value,name,record in requested_addresses:
            if any(base<=value<base+size for base,size in ranges):
                record['protocolContext']=cache.fault_context(image,value,symbols,preferred_name=name,context_bytes=args.lookup_bytes)
        for cls,selector in methods:
            try:
                method=cache.objc_method(image,cls,selector)
                value=int(method['address'],16);boundaries=method.pop('boundaries')
                label=f'-[{cls} {selector}]'
                declared=symbols+[(value,label)]
                end=min((address for address in boundaries if address>value),default=None)
                records.append({'image':image['name'],'objcMethod':method,
                    'protocolContext':cache.fault_context(image,value,declared,preferred_name=label,end_bound=end,context_bytes=args.lookup_bytes)})
            except ValueError as error:
                records.append({'image':image['name'],'objcMethod':{'class':cls,'selector':selector},
                                'status':'metadata-unavailable','reason':str(error)})
        for pc in matches:
            matched.add(pc)
            address=lookup_pc(pc)
            nearest=nearest_symbol(symbols,address)
            record={'pc':hex(pc),'image':image['name'],'imageAddress':hex(image['address'])}
            if address!=pc:record.update(lookupAddress=hex(address),note='Saved return address; symbolize its actual ARM64 call instruction')
            if nearest:record.update(nearestSymbol=nearest[1],symbolAddress=hex(nearest[0]),displacement=address-nearest[0])
            if pc in faults or (pc not in unsupported_returns and ((pc in returns and returns[pc]<=3) or (nearest and 'display' in nearest[1].lower()))):
                record['faultContext']=cache.fault_context(image,address,symbols)
            elif pc not in unsupported_returns and ('UIKitCore.framework/' in image['name'] or 'GraphicsServices.framework/' in image['name']):
                record['waitContext']=cache.fault_context(image,address,symbols)
            records.append(record)
    for pc in frames:
        if pc not in matched:records.append({'pc':hex(pc),'status':'outside-cache','note':'Requires original app/interpreter or host symbols'})
args.output.write_text(json.dumps(records,indent=2)+'\n')
for record in records:print('CACHE_SYMBOLICATION '+json.dumps(record),flush=True)
