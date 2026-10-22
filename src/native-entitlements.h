/* Host-side serialization of the original XML entitlement dictionary into
 * Apple's legacy DER plist representation. No grants are added or inferred.
 * Format reference: ProcursusTeam/ldid af86971, der(plist_t); independently
 * implemented with explicit input/depth/output bounds and signed integers. */
#pragma once
#include <plist/plist.h>
enum { NATIVE_ENTITLEMENT_LIMIT=1024*1024, NATIVE_ENTITLEMENT_DEPTH=32,
       NATIVE_ENTITLEMENT_NODES=4096 };
struct native_der { unsigned char *bytes; size_t size; };
static void native_der_append(struct native_der *out,const void *bytes,size_t size)
{
    need(size<=NATIVE_ENTITLEMENT_LIMIT-out->size,"DER entitlement size limit");
    if(!size)return;
    unsigned char *grown=realloc(out->bytes,out->size+size);
    need(grown!=NULL,"DER entitlement allocation");out->bytes=grown;
    memcpy(out->bytes+out->size,bytes,size);out->size+=size;
}
static struct native_der native_der_wrap(unsigned char tag,struct native_der body)
{
    struct native_der out={0};unsigned char header[6]={tag};size_t count=2;
    if(body.size<128)header[1]=(unsigned char)body.size;
    else {
        size_t value=body.size,n=0;
        do {n++;value>>=8;}while(value);
        header[1]=0x80|(unsigned char)n;count+=n;
        for(size_t i=0;i<n;i++)header[2+i]=(unsigned char)(body.size>>(8*(n-1-i)));
    }
    native_der_append(&out,header,count);native_der_append(&out,body.bytes,body.size);
    free(body.bytes);return out;
}
static struct native_der native_der_scalar(unsigned char tag,const void *bytes,size_t size)
{
    struct native_der body={0};native_der_append(&body,bytes,size);
    return native_der_wrap(tag,body);
}
static int native_der_compare(const void *left,const void *right)
{
    const struct native_der *a=left,*b=right;
    size_t common=a->size<b->size ? a->size : b->size;
    int order=memcmp(a->bytes,b->bytes,common);
    return order ? order : (a->size>b->size)-(a->size<b->size);
}
static struct native_der native_der_plist(plist_t node,unsigned int depth,unsigned int *nodes)
{
    need(node && depth<=NATIVE_ENTITLEMENT_DEPTH && ++*nodes<=NATIVE_ENTITLEMENT_NODES,
         "DER entitlement nesting/node limit");
    switch(plist_get_node_type(node)) {
    case PLIST_BOOLEAN: {
        uint8_t value=0;plist_get_bool_val(node,&value);value=value ? 0xff : 0;
        return native_der_scalar(1,&value,1);
    }
    case PLIST_UINT: {
        uint64_t value=0;plist_get_uint_val(node,&value);unsigned char bytes[8];
        for(unsigned int i=0;i<8;i++)bytes[i]=(unsigned char)(value>>(8*(7-i)));
        size_t start=0;
        while(start<7 && ((bytes[start]==0 && !(bytes[start+1]&0x80)) ||
              (bytes[start]==0xff && (bytes[start+1]&0x80))))start++;
        return native_der_scalar(2,bytes+start,8-start);
    }
    case PLIST_STRING: case PLIST_DATA: {
        char *bytes=NULL;uint64_t size=0;int string=plist_get_node_type(node)==PLIST_STRING;
        if(string) {plist_get_string_val(node,&bytes);if(bytes)size=strlen(bytes);}
        else plist_get_data_val(node,&bytes,&size);
        need(size<=NATIVE_ENTITLEMENT_LIMIT && (bytes || !size),"DER entitlement scalar bounds");
        struct native_der out=native_der_scalar(string ? 0x0c : 4,bytes,(size_t)size);
        free(bytes);return out;
    }
    case PLIST_ARRAY: {
        struct native_der body={0};uint32_t count=plist_array_get_size(node);
        need(count<=NATIVE_ENTITLEMENT_NODES,"DER entitlement array bounds");
        for(uint32_t i=0;i<count;i++) {
            struct native_der child=native_der_plist(plist_array_get_item(node,i),depth+1,nodes);
            native_der_append(&body,child.bytes,child.size);free(child.bytes);
        }
        return native_der_wrap(0x30,body);
    }
    case PLIST_DICT: {
        uint32_t count=plist_dict_get_size(node);
        need(count<=NATIVE_ENTITLEMENT_NODES,"DER entitlement dictionary bounds");
        struct native_der *entries=calloc(count ? count : 1,sizeof(*entries));
        need(entries!=NULL,"DER entitlement entries allocation");
        plist_dict_iter iterator=NULL;plist_dict_new_iter(node,&iterator);
        for(uint32_t i=0;i<count;i++) {
            char *key=NULL;plist_t value=NULL;plist_dict_next_item(node,iterator,&key,&value);
            need(key && value,"DER entitlement dictionary iteration");
            struct native_der pair=native_der_scalar(0x0c,key,strlen(key));free(key);
            struct native_der child=native_der_plist(value,depth+1,nodes);
            native_der_append(&pair,child.bytes,child.size);free(child.bytes);
            entries[i]=native_der_wrap(0x30,pair);
        }
        free(iterator);qsort(entries,count,sizeof(*entries),native_der_compare);
        struct native_der body={0};
        for(uint32_t i=0;i<count;i++) {native_der_append(&body,entries[i].bytes,entries[i].size);free(entries[i].bytes);}
        free(entries);return native_der_wrap(0x31,body);
    }
    default:need(0,"unsupported XML entitlement value type");return (struct native_der){0};
    }
}
static void native_launch_entitlements(struct darwin_code_metadata *code)
{
    if(code->der || !code->xml)return;
    need(code->xml_bytes>8 && code->xml_bytes<=NATIVE_ENTITLEMENT_LIMIT,"XML entitlement bounds");
    plist_t metadata=NULL;plist_from_xml((const char *)code->xml+8,code->xml_bytes-8,&metadata);
    need(metadata && plist_get_node_type(metadata)==PLIST_DICT,"invalid entitlement dictionary");
    unsigned int nodes=0;struct native_der encoded=native_der_plist(metadata,0,&nodes);plist_free(metadata);
    need(encoded.size<=NATIVE_ENTITLEMENT_LIMIT-8,"DER entitlement blob bounds");
    size_t bytes=8+encoded.size,allocation=round16(bytes);
    unsigned char *blob=mmap(NULL,allocation,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    need(blob!=MAP_FAILED,"DER entitlement mapping");
    const unsigned char magic[4]={0xfa,0xde,0x71,0x72};memcpy(blob,magic,4);
    for(unsigned int i=0;i<4;i++)blob[4+i]=(unsigned char)(bytes>>(8*(3-i)));
    memcpy(blob+8,encoded.bytes,encoded.size);free(encoded.bytes);
    need(!mprotect(blob,allocation,PROT_READ),"DER entitlement readonly mapping");
    code->der=blob;code->der_bytes=(uint32_t)bytes;
    fprintf(stderr,"NATIVE_ENTITLEMENTS_TRANSMUTED xml_bytes=%u der_bytes=%u nodes=%u source=embedded-xml\n",
        code->xml_bytes,code->der_bytes,nodes);
}
