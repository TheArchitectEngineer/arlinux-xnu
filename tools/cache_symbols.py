"""Runner-only split-cache reader. Layout: Apple's dyld-1066.10 format header
and DyldSharedCache::forEachLocalSymbolEntry. No executable data is exported.
"""
from contextlib import ExitStack
import mmap
import struct
import subprocess

def symbol_preference(item):
    value,name=item
    return value,bool(name and name!='<redacted>'),name or ''

def nearest_symbol(symbols,address):
    # Prefer a real declared alias over <redacted> at the SAME address only.
    # This does not make a nearest symbol an exact function/call attribution.
    return max((item for item in symbols if item[0]<=address),key=symbol_preference,default=None)

class Binary:
    def __init__(self,path,stack):
        stream=stack.enter_context(path.open('rb'))
        self.data=stack.enter_context(mmap.mmap(stream.fileno(),0,access=mmap.ACCESS_READ))
        self.path=path
    def bounded(self,offset,size):
        if offset<0 or size<0 or offset+size>len(self.data):
            raise ValueError(f'Invalid metadata: {self.path.name} {offset:#x}+{size:#x}')
        return offset
    def unpack(self,fmt,offset):
        return struct.unpack_from(fmt,self.data,self.bounded(offset,struct.calcsize(fmt)))
    def string(self,offset,maximum=8192):
        self.bounded(offset,1)
        end=self.data.find(b'\0',offset,min(offset+maximum,len(self.data)))
        if end<0:raise ValueError('Unterminated cache string')
        return self.data[offset:end].decode('utf-8',errors='replace')

class CacheSymbols:
    def __init__(self,path):
        self.path=path;self.stack=ExitStack()
    def __enter__(self):
        try:
            p=self.primary=Binary(self.path,self.stack)
            self.files=[p]
            if p.unpack('<I',16)[0]<512:raise ValueError('Expected declared modern cache format')
            self.images_offset,self.images_count=p.unpack('<II',448)
            if not 0<self.images_count<=16384:raise ValueError('Invalid image count')
            p.bounded(self.images_offset,self.images_count*32)
            offset,count=p.unpack('<II',392)
            if count>=128:raise ValueError('Too many cache members')
            p.bounded(offset,count*56)
            self.base=p.unpack('<Q',224)[0]
            for i in range(count):
                uuid,vm,suffix=p.unpack('<16sQ32s',offset+i*56)
                if b'\0' not in suffix:raise ValueError('Unterminated member suffix')
                suffix=suffix.split(b'\0',1)[0].decode('ascii')
                if not suffix.startswith('.') or '/' in suffix or '\\' in suffix:raise ValueError('Unsafe cache suffix')
                member=Binary(self.path.with_name(self.path.name+suffix),self.stack)
                if member.data[88:104]!=uuid:raise ValueError('Cache member UUID mismatch')
                if member.unpack('<Q',member.unpack('<I',16)[0])[0]!=self.base+vm:raise ValueError('Cache VM mismatch')
                self.files.append(member)
            self.mappings=[];self.slides=[]
            for member in self.files:
                if member.data[:16]!=b'dyld_v1   arm64\0':raise ValueError('Unsupported cache architecture')
                offset,count=member.unpack('<II',16)
                if not 0<count<=512:raise ValueError('Invalid mapping count')
                member.bounded(offset,count*32)
                for i in range(count):
                    address,size,file_offset,_,_=member.unpack('<QQQII',offset+i*32)
                    member.bounded(file_offset,size)
                    if any(address<old+length and old<address+size for old,length,_,_ in self.mappings):raise ValueError('Overlapping mappings')
                    self.mappings.append((address,size,member,file_offset))
                slide_offset,slide_count=member.unpack('<II',312)
                if slide_count!=count:raise ValueError('Slide mapping count mismatch')
                member.bounded(slide_offset,slide_count*56)
                for i in range(slide_count):
                    base,length,file_offset,info,info_size,_,_,_=member.unpack('<QQQQQQII',slide_offset+i*56)
                    if not any(base==old and length==size and member is file and file_offset==offset
                               for old,size,file,offset in self.mappings):raise ValueError('Slide mapping identity mismatch')
                    if not info_size:continue
                    member.bounded(info,info_size)
                    if info_size<40 or member.unpack('<I',info)[0]!=2:raise ValueError('Unsupported diagnostic pointer format')
                    mask,add=member.unpack('<QQ',info+24)
                    if not mask:raise ValueError('Invalid slide pointer mask')
                    self.slides.append((base,length,mask,add))
            self.locals={}
            uuid=p.data[400:416]
            if any(uuid):
                member=Binary(self.path.with_name(self.path.name+'.symbols'),self.stack)
                if member.data[88:104]!=uuid:raise ValueError('Local-symbol UUID mismatch')
                offset,size=member.unpack('<QQ',72);member.bounded(offset,size)
                noff,ncount,soff,ssize,eoff,ecount=member.unpack('<IIIIII',offset)
                if noff+ncount*16>size or soff+ssize>size or eoff+ecount*16>size:raise ValueError('Invalid local-symbol bounds')
                for i in range(ecount):
                    vm,first,count=member.unpack('<QII',offset+eoff+i*16)
                    if first+count>ncount:raise ValueError('Invalid local-symbol indexes')
                    self.locals[self.base+vm]=(member,offset+noff+first*16,count,offset+soff,ssize)
            return self
        except BaseException:
            self.stack.close();raise
    def __exit__(self,*args):return self.stack.__exit__(*args)
    def locate(self,address,size):
        for base,length,member,offset in self.mappings:
            if base<=address and size<=length-(address-base):return member,offset+address-base
        raise ValueError(f'Unmapped cache data {address:#x}+{size:#x}')
    def images(self):
        seen=set()
        for i in range(self.images_count):
            address,_,_,path,_=self.primary.unpack('<QQQII',self.images_offset+i*32)
            if address in seen:continue
            seen.add(address);name=self.primary.string(path)
            member,start=self.locate(address,32)
            magic,cpu,subtype,kind,count,size,_,_=member.unpack('<IIIIIIII',start)
            if magic!=0xfeedfacf or cpu!=0x100000c or subtype&0xffffff or kind not in (6,7) or count>256:raise ValueError('Unsupported image: '+name)
            self.locate(address,32+size)
            cursor,end=start+32,start+32+size
            executable=[];cstring=[];cfstring=[];classrefs=[];linkedit=None;tables=[];objc_opt_ro=None
            for _ in range(count):
                cmd,size=member.unpack('<II',cursor)
                if size<8 or size%8 or cursor+size>end:raise ValueError('Invalid command: '+name)
                if cmd==0x19:
                    if size<72:raise ValueError('Truncated segment')
                    segment=member.unpack('<16sQQQQIIII',cursor+8)
                    if segment[0].rstrip(b'\0')==b'__LINKEDIT':linkedit=segment[1:5]
                    if segment[6]&4:executable.append((segment[1],segment[2]))
                    sections=segment[7]
                    if sections>128 or 72+sections*80>size:raise ValueError('Invalid section table')
                    for n in range(sections):
                        section=cursor+72+n*80
                        if member.unpack('<I',section+64)[0]&255==2: # S_CSTRING_LITERALS
                            cstring.append(member.unpack('<QQ',section+32))
                        if member.data[section:section+16].rstrip(b'\0')==b'__cfstring':
                            cfstring.append(member.unpack('<QQ',section+32))
                        if member.data[section:section+16].rstrip(b'\0')==b'__objc_classrefs':
                            classrefs.append(member.unpack('<QQ',section+32))
                        if member.data[section:section+16].rstrip(b'\0')==b'__objc_opt_ro':
                            objc_opt_ro=member.unpack('<QQ',section+32)
                elif cmd==2:
                    if size!=24:raise ValueError('Invalid symbol command')
                    tables.append(member.unpack('<IIII',cursor+8))
                cursor+=size
            if cursor!=end:raise ValueError('Invalid command extent')
            yield {'name':name,'address':address,'executable':executable,'cstring':cstring,'cfstring':cfstring,'classrefs':classrefs,'linkedit':linkedit,'tables':tables,'objcOptRO':objc_opt_ro}

    def objc_method(self,image,class_name,selector):
        """Resolve an instance IMP from declared Objective-C metadata, never a call.

        Apple ObjCVisitor.h/cpp class_t, ClassData, method_list_t and
        DyldSharedCache::objcRelativeMethodListsBaseAddress define these fields.
        Export only the requested name/address, not metadata or executable bytes.
        """
        def unpack(address,fmt):
            member,offset=self.locate(address,struct.calcsize(fmt))
            return member.unpack(fmt,offset)
        def string(address):
            member,offset=self.locate(address,1)
            return member.string(offset,512)
        def selector_base():
            offset,size=self.files[0].unpack('<QQ',464)
            if offset and size>=56:
                address=self.base+offset
                version=unpack(address,'<I')[0]
                if version!=1:raise ValueError('Unknown ObjC optimization header')
                relative=unpack(address+48,'<Q')[0]
                if relative:return self.base+relative
            objc=next((entry for entry in self.images() if entry['name']=='/usr/lib/libobjc.A.dylib'),None)
            region=objc and objc['objcOptRO']
            if not region or region[1]<48:raise ValueError('No declared selector base')
            address=region[0]
            if unpack(address,'<I')[0]!=16:raise ValueError('Unknown legacy ObjC optimization header')
            relative=unpack(address+40,'<q')[0]
            if not relative:raise ValueError('Empty selector base')
            return address+relative
        classes=[value for value,name in self.symbols(image) if name=='_OBJC_CLASS_$_'+class_name]
        if len(set(classes))!=1:raise ValueError('Missing or ambiguous declared Objective-C class')
        class_address=classes[0]
        ro=self.typed_pointer(class_address+32)&0x00007ffffffffff8
        if string(self.typed_pointer(ro+24))!=class_name:raise ValueError('Class metadata name mismatch')
        methods=self.typed_pointer(ro+32)
        flags,count=unpack(methods,'<II')
        relative=bool(flags&0x80000000);direct=bool(flags&0x40000000)
        stride=flags&0x0000fffc
        if stride!=(12 if relative else 24) or count>4096:raise ValueError('Unknown method-list layout')
        self.locate(methods+8,count*stride)
        base=selector_base() if relative and direct else None
        matches=[];boundaries=[]
        for i in range(count):
            entry=methods+8+i*stride
            if relative:
                name_offset,_,imp_offset=unpack(entry,'<iii')
                name=base+unpack(entry,'<I')[0] if direct else self.typed_pointer(entry+name_offset)
                imp=entry+8+imp_offset
            else:
                name=self.typed_pointer(entry);imp=self.typed_pointer(entry+16)
            inside=any(begin<=imp<begin+size for begin,size in image['executable'])
            if inside:boundaries.append(imp)
            if string(name)==selector:
                if not inside:raise ValueError('Requested method implementation outside declared executable image')
                matches.append(imp)
        if len(set(matches))!=1:raise ValueError('Missing or ambiguous requested instance method')
        return {'class':class_name,'selector':selector,'classAddress':hex(class_address),
                'readOnlyData':hex(ro),'methodList':hex(methods),'methodListFlags':hex(flags),
                'methodCount':count,'address':hex(matches[0]),'boundaries':boundaries,
                'source':'Declared class_ro.baseMethods; no injected guest calls'}

    def typed_pointer(self,address):
        """Read a declared pointer field with its mapping's actual slide-v2 data."""
        member,offset=self.locate(address,8)
        value=member.unpack('<Q',offset)[0]
        for base,size,mask,add in self.slides:
            if base<=address<base+size:
                value&=~mask
                return value+add if value else 0
        return value

    def return_call(self,pc):
        """Resolve only an actual BL and bounded B / ADRP-LDR-BR cache stubs."""
        member,offset=self.locate(pc-4,4);word=member.unpack('<I',offset)[0]
        if word&0xfc000000!=0x94000000:return None
        def branch(address,word):
            immediate=word&0x3ffffff
            if immediate&0x2000000:immediate-=0x4000000
            return address+immediate*4
        target=branch(pc-4,word);hops=[]
        for _ in range(3):
            member,offset=self.locate(target,12)
            first,second,third=member.unpack('<III',offset)
            if first&0xfc000000==0x14000000:
                following=branch(target,first)
            elif first&0x9f000000==0x90000000 and second&0xffc00000==0xf9400000 and third&0xfffffc1f==0xd61f0000:
                base=first&31;destination=second&31
                if (second>>5)&31!=base or (third>>5)&31!=destination:break
                immediate=((first>>5)&0x7ffff)<<2 | ((first>>29)&3)
                if immediate&0x100000:immediate-=0x200000
                slot=(target&~4095)+(immediate<<12)+((second>>10)&4095)*8
                following=self.typed_pointer(slot)
            else:break
            hops.append({'stub':hex(target),'target':hex(following)})
            if following==target:break
            target=following
        return {'returnPC':hex(pc),'instruction':hex(pc-4),'target':hex(target),'stubHops':hops}

    def constant_string(self,image,address):
        """Only a declared 32-byte constant CFString backed by its C-string section."""
        if not any(base<=address and address+32<=base+size and (address-base)%32==0
                   for base,size in image['cfstring']):return None
        member,offset=self.locate(address,32)
        length=member.unpack('<Q',offset+24)[0]
        if length>512:return None
        pointer=self.typed_pointer(address+16)
        if not any(base<=pointer and pointer+length+1<=base+size for base,size in image['cstring']):return None
        member,offset=self.locate(pointer,length+1)
        data=member.data[offset:offset+length+1]
        if data[-1] or b'\0' in data[:-1]:return None
        return {'object':hex(address),'address':hex(pointer),'literal':data[:-1].decode('utf-8',errors='replace'),
                'note':'Declared constant CFString; pointer decoded with actual mapping slide metadata'}
    def symbols(self,image):
        tables=[]
        for offset,count,strings,size in image['tables']:
            if not image['linkedit']:raise ValueError('Missing linkedit')
            base,_,file_offset,file_size=image['linkedit']
            if offset<file_offset or offset+count*16>file_offset+file_size or strings<file_offset or strings+size>file_offset+file_size:raise ValueError('Invalid image symbol bounds')
            member,nlist=self.locate(base+offset-file_offset,count*16)
            smember,sstart=self.locate(base+strings-file_offset,size)
            tables.append((member,nlist,count,smember,sstart,size))
        if image['address'] in self.locals:
            member,offset,count,strings,size=self.locals[image['address']]
            tables.append((member,offset,count,member,strings,size))
        for member,offset,count,smember,strings,size in tables:
            for i in range(count):
                index,kind,section,_,value=member.unpack('<IBBHQ',offset+i*16)
                if kind&0xe0 or kind&0xe!=0xe or not section or index>=size:continue
                yield value,smember.string(strings+index,min(8192,size-index))

    def disassemble(self,address,size):
        """Bounded, read-only instructions from the UUID-matched cache."""
        if size<=0 or size>4096 or size%4:raise ValueError('Invalid diagnostic code extent')
        member,offset=self.locate(address,size)
        code=member.data[offset:offset+size]
        source='\n'.join(' '.join(f'0x{byte:02x}' for byte in code[i:i+4]) for i in range(0,len(code),4))
        try:
            process=subprocess.run(['llvm-mc-18','--disassemble','--triple=aarch64','--print-imm-hex'],
                input=source,text=True,capture_output=True,timeout=5,check=True)
        except (subprocess.TimeoutExpired,subprocess.CalledProcessError) as error:
            return {'address':hex(address),'bytes':size,'instructions':'',
                'status':'decoder-timeout' if isinstance(error,subprocess.TimeoutExpired) else 'decoder-failed',
                'note':'Instruction decoding unavailable; original addresses and independent metadata remain evidence'}
        return {'address':hex(address),'bytes':size,'instructions':process.stdout,
                'decoderDiagnostics':process.stderr,
                'note':'Branch immediates are relative to their instruction; observation only'}

    def fault_context(self,image,pc,symbols,preferred_name=None,end_bound=None,context_bytes=None):
        """Original fault's bounded function and named direct callees, never replay."""
        base,length=next((base,length) for base,length in image['executable'] if base<=pc<base+length)
        nearest=nearest_symbol(symbols,pc) or (pc,None)
        if preferred_name and (pc,preferred_name) in symbols:nearest=(pc,preferred_name)
        start=max(nearest[0],pc-512,base)
        # Display::update inlines its MIG decoder. Keep its full bounded
        # descriptor/string validation rather than cutting it after a header.
        maximum=context_bytes if context_bytes is not None else (4096 if nearest[1] and ('display' in nearest[1].lower() or 'ProcessSnapshot10Serializer' in nearest[1] or 'xpc_pipe_' in nearest[1] or 'bsxpc' in nearest[1].lower()) else 768)
        if maximum<=0 or maximum>4096 or maximum%4:raise ValueError('Invalid diagnostic context budget')
        end=min(start+maximum,base+length)
        following=min((value for value,name in symbols if value>pc),default=base+length)
        end=min(end,following,end_bound if end_bound is not None else end)
        record=self.disassemble(start,end-start)
        record.update(symbol=nearest[1],faultPC=hex(pc),symbolAliases=[name for value,name in symbols if value==nearest[0]],
                      byteLimit=maximum,limitedByByteBudget=end<min(base+length,following,end_bound if end_bound is not None else base+length))
        literals=[];classes=[]
        member,offset=self.locate(start,end-start)
        for i in range(0,end-start-4,4):
            word=member.unpack('<I',offset+i)[0]
            after=member.unpack('<I',offset+i+4)[0]
            # ADRP Xn; ADD Xn,Xn,#imm: exact address, not a text search or
            # speculative dereference. Only declared C-string sections count.
            register=word&31
            if word&0x9f000000!=0x90000000 or (after>>5)&31!=register:continue
            immediate=((word>>5)&0x7ffff)<<2 | ((word>>29)&3)
            if immediate&0x100000:immediate-=0x200000
            page=((start+i)&~4095)+(immediate<<12)
            if after&0xffc00000==0xf9400000: # ADRP; LDR Xd,[Xn,#imm*8]
                slot=page+((after>>10)&4095)*8
                if any(begin<=slot and slot+8<=begin+size and (slot-begin)%8==0 for begin,size in image['classrefs']):
                    entry={'instruction':hex(start+i),'slot':hex(slot)}
                    try:
                        target=self.typed_pointer(slot)
                        entry['classAddress']=hex(target)
                        if target:
                            ro=self.typed_pointer(target+32)&0x00007ffffffffff8
                            name=self.typed_pointer(ro+24)
                            cmember,coffset=self.locate(name,1)
                            entry['className']=cmember.string(coffset,512)
                        else:entry['status']='null-class-reference'
                    except ValueError as error:entry.update(status='metadata-unavailable',reason=str(error))
                    classes.append(entry)
                continue
            if after&0xffc00000!=0x91000000 or after&31!=register:continue
            address=page+((after>>10)&4095)
            constant=self.constant_string(image,address)
            if constant:literals.append({'instruction':hex(start+i),**constant})
            for cbase,csize in image['cstring']:
                if not cbase<=address<cbase+csize:continue
                maximum=min(512,cbase+csize-address)
                string_member,string_offset=self.locate(address,maximum)
                data=string_member.data[string_offset:string_offset+maximum]
                literals.append({'instruction':hex(start+i),'address':hex(address),
                    'literal':data.split(b'\0',1)[0].decode('utf-8',errors='replace'),'truncated':b'\0' not in data})
        record['referencedLiterals']=literals
        record['referencedClasses']=classes
        names={}
        for value,name in symbols:
            if value not in names or symbol_preference((value,name))>symbol_preference((value,names[value])):
                names[value]=name
        def direct_calls(begin,size):
            calls={};member,offset=self.locate(begin,size)
            for i in range(0,size,4):
                word=member.unpack('<I',offset+i)[0]
                if word&0xfc000000 not in (0x94000000,0x14000000):continue
                immediate=word&0x3ffffff
                if immediate&0x2000000:immediate-=0x4000000
                target=begin+i+immediate*4
                # Tail branches to named functions are part of the interface
                # too; ordinary local loop/control-flow branches are not calls.
                if word&0xfc000000==0x14000000 and begin<=target<begin+size:continue
                if target in names:calls[target]=names[target]
            return calls
        calls=direct_calls(start,end-start)
        # A late assertion may be far from the MIG request construction.
        # Preserve a separate bounded entry window of that same real function.
        if nearest[0]<start:
            entry_size=min(768,start-nearest[0])
            record['functionEntry']=self.disassemble(nearest[0],entry_size)
            calls.update(direct_calls(nearest[0],entry_size))
        record['directCalls']=[{'address':hex(address),'symbol':name} for address,name in sorted(calls.items())]
        callees=[];pending=[(address,name,1) for address,name in sorted(calls.items())];seen={start}
        while pending:
            address,name,depth=pending.pop(0)
            if address in seen:continue
            seen.add(address)
            if not any(part in name.lower() for part in ('bks','screen','display','xpc_pipe','bsxpc')):continue
            if len(callees)>=8:break
            region=next(((base,length) for base,length in image['executable'] if base<=address<base+length),None)
            if not region:continue
            limit=4096 if any(part in name.lower() for part in ('xpc_pipe','bsxpc')) else 768
            stop=min(address+limit,region[0]+region[1],min((value for value,_ in symbols if value>address),default=address+limit))
            entry=self.disassemble(address,stop-address);entry['symbol']=name
            children=direct_calls(address,stop-address)
            entry['directCalls']=[{'address':hex(value),'symbol':label} for value,label in sorted(children.items())]
            callees.append(entry)
            if depth<3:pending.extend((value,label,depth+1) for value,label in sorted(children.items()))
        record['protocolCallees']=callees
        return record

    def bootstrap_context(self,image):
        """Read-only disassembly around actual libxpc audit/pid predicates.
        It records candidates; it neither selects a patch nor approves a layout.
        """
        symbols=list(self.symbols(image))
        getters={value for value,name in symbols if 'audit_token' in name and 'get' in name}
        candidates=[]
        for base,size in image['executable']:
            member,offset=self.locate(base,size)
            for i in range(16,size-192,4):
                word=member.unpack('<I',offset+i)[0]
                if word&0xfc000000==0x94000000:
                    immediate=word&0x3ffffff
                    if immediate&0x2000000:immediate-=0x4000000
                    target=base+i+immediate*4
                    if target in getters:candidates.append((base+i-12,'audit-token getter call'))
                # CMP Wn,#1 following LDR Wn,[SP,#offset]. Bound the register
                # relation rather than guessing an iOS version or fixed address.
                if word&0xfffffc1f==0x7100041f:
                    prior=member.unpack('<I',offset+i-4)[0]
                    if prior&0xffc003e0==0xb94003e0 and prior&31==(word>>5)&31:
                        candidates.append((base+i-16,'stack value compared with pid 1'))
        unique={address:reason for address,reason in candidates}
        if len(unique)>32:raise ValueError('Ambiguous bootstrap diagnostic scope exceeds 32 windows')
        result=[]
        for address,reason in sorted(unique.items()):
            nearest=nearest_symbol(symbols,address)
            result.append({**self.disassemble(address,192),'reason':reason,'image':image['name'],
                'nearestSymbol':nearest[1] if nearest else None,
                'symbolDisplacement':address-nearest[0] if nearest else None})
        return result
