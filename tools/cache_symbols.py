"""Runner-only split-cache reader. Layout: Apple's dyld-1066.10 format header
and DyldSharedCache::forEachLocalSymbolEntry. No executable data is exported.
"""
from contextlib import ExitStack
import mmap
import struct
import subprocess

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
            self.mappings=[]
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
            executable=[];linkedit=None;tables=[]
            for _ in range(count):
                cmd,size=member.unpack('<II',cursor)
                if size<8 or size%8 or cursor+size>end:raise ValueError('Invalid command: '+name)
                if cmd==0x19:
                    if size<72:raise ValueError('Truncated segment')
                    segment=member.unpack('<16sQQQQIIII',cursor+8)
                    if segment[0].rstrip(b'\0')==b'__LINKEDIT':linkedit=segment[1:5]
                    if segment[6]&4:executable.append((segment[1],segment[2]))
                elif cmd==2:
                    if size!=24:raise ValueError('Invalid symbol command')
                    tables.append(member.unpack('<IIII',cursor+8))
                cursor+=size
            if cursor!=end:raise ValueError('Invalid command extent')
            yield {'name':name,'address':address,'executable':executable,'linkedit':linkedit,'tables':tables}
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

    def bootstrap_context(self,image):
        """Read-only disassembly around actual libxpc audit/pid predicates.
        It records candidates; it neither selects a patch nor approves a layout.
        """
        symbols=list(self.symbols(image))
        getters={value for value,name in symbols if 'audit_token' in name and 'get' in name}
        candidates=[]
        for base,size in image['executable']:
            member,offset=self.locate(base,size)
            for i in range(12,size-96,4):
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
            member,offset=self.locate(address,96)
            code=member.data[offset:offset+96]
            source='\n'.join(' '.join(f'0x{byte:02x}' for byte in code[i:i+4]) for i in range(0,len(code),4))
            process=subprocess.run(['llvm-mc-18','--disassemble','--triple=aarch64','--print-imm-hex'],
                input=source,text=True,capture_output=True,timeout=5,check=True)
            nearest=max(((value,name) for value,name in symbols if value<=address),default=None)
            result.append({'address':hex(address),'reason':reason,'image':image['name'],
                'nearestSymbol':nearest[1] if nearest else None,
                'symbolDisplacement':address-nearest[0] if nearest else None,
                'instructions':process.stdout,'decoderDiagnostics':process.stderr,
                'note':'Branch immediates are relative to their instruction; observation only'})
        return result
