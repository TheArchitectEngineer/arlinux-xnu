"""Bounds-checked cache provenance, not app acceptance or a replacement dyld.
Field layout: Apple's dyld_cache_format.h. Old/new fields are selected by header
geometry and actual table presence, never by an iOS marketing version string.
"""
import struct

def describe_cache(path,sha):
    result={'path':str(path),'files':[]}
    for file in sorted(path.parent.glob(path.name+'*')):
        if not file.is_file():continue
        with file.open('rb') as stream:header=stream.read(16384)
        if not header.startswith(b'dyld_'):continue
        size=file.stat().st_size
        def unpack(fmt,offset):
            if offset<0 or offset+struct.calcsize(fmt)>len(header):raise ValueError('Truncated cache header')
            return struct.unpack_from(fmt,header,offset)
        offset,count,images,number=unpack('<IIII',16)
        if not number and offset>=456:images,number=unpack('<II',448)
        record={'name':file.name,'bytes':size,'sha256':sha(file),'magic':header[:16].decode(errors='replace'),
                'uuid':header[88:104].hex(),'imagesOffset':images,'imagesCount':number,'mappings':[]}
        if offset>=512:
            dynamic,maximum=unpack('<QQ',496)
            record['dynamicData']={'offset':dynamic,'maximumSize':maximum}
        with file.open('rb') as stream:
            if count>1024 or offset+count*32>size:raise ValueError('Invalid cache mapping table')
            stream.seek(offset)
            for _ in range(count):
                record['mappings'].append(dict(zip(['address','size','offset','maxProtection','protection'],struct.unpack('<QQQII',stream.read(32)))))
            if offset>=320:
                start,total=unpack('<II',312)
                if total>1024 or start+total*56>size:raise ValueError('Invalid cache slide mapping table')
                stream.seek(start);record['slideMappings']=[]
                for _ in range(total):
                    entry=dict(zip(['address','size','offset','slideOffset','slideSize','flags','maxProtection','protection'],struct.unpack('<QQQQQQII',stream.read(56))))
                    record['slideMappings'].append(entry)
                for entry in record['slideMappings']:
                    if entry['slideSize']:
                        if entry['slideOffset']+entry['slideSize']>size:raise ValueError('Invalid cache slide range')
                        stream.seek(entry['slideOffset']);entry['slideVersion']=struct.unpack('<I',stream.read(4))[0]
        result['files'].append(record)
    return result
