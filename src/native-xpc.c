/* Host-TLS service thread only. Codec: pinned, unmodified MIT libspc.
 * Connection/message rights remain in Darling/XNU. No guest libc is called. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "darwin-native.h"
#include "datatypes.h"
#include "dictionary.h"
#include "serialization.h"

mach_port_t mach_task_self(void) { return darwin_native_host_task(); }
int mach_port_deallocate(mach_port_t task,mach_port_t port)
{ (void)task; return darwin_native_host_port_refs(port,-1); }
int mach_port_addref(mach_port_t port,mach_port_right_t right)
{ return right==MACH_PORT_RIGHT_SEND ? darwin_native_host_port_refs(port,1) : D_KERN_NOT_SUPPORTED; }
int fileport_makeport(int fd,mach_port_t *port)
{ (void)fd; (void)port; return D_ENOTSUP; }

int arlinux_xpc_bootstrap(void *request,void *output,size_t capacity,uint32_t bootstrap,uint32_t *held_port)
{
    mach_msg_header_t *header=request;
    if (header->msgh_id!=0x10000000) return 0;
    /* The selected upstream decoder ignores OOL descriptors. Do not silently
     * pass them through it. Its fatal malformed-input behavior remains visible
     * for this trusted-firmware experiment; arbitrary peers are unsupported. */
    if ((header->msgh_bits&MACH_MSGH_BITS_COMPLEX) || header->msgh_size<32) {
        fprintf(stderr,"BOOTSTRAP_XPC_UNSUPPORTED bits=%x size=%u\n",header->msgh_bits,header->msgh_size);
        abort();
    }
    spc_message_t *message=spc_deserialize(request);
    if (!message) { fprintf(stderr,"BOOTSTRAP_XPC_INVALID\n"); abort(); }
    uint64_t subsystem=spc_dictionary_get_uint64(message->content,"subsystem");
    uint64_t routine=spc_dictionary_get_uint64(message->content,"routine");
    const char *name=spc_dictionary_get_string(message->content,"name");
    fprintf(stderr,"BOOTSTRAP_XPC_REQUEST subsystem=%llu routine=%llu name=%s\n",
        (unsigned long long)subsystem,(unsigned long long)routine,name ? name : "<absent>");
    for (spc_dictionary_item_t *item=message->content->items;item;item=item->next) {
        if (item->value.type==SPC_TYPE_UINT64 || item->value.type==SPC_TYPE_INT64)
            fprintf(stderr,"BOOTSTRAP_XPC_FIELD key=%s type=%x value=%llu\n",item->key,item->value.type,
                (unsigned long long)item->value.value.u64);
    }
    int error=D_ENOTSUP;
    uint32_t port=0;
    /* libspc connection.c: spc_domain_routine / spc_look_up_endpoint.
     * Only this process's default namespace is currently implemented. */
    if (subsystem==3 && routine==0x324 && name &&
        !spc_dictionary_get_uint64(message->content,"handle") &&
        !spc_dictionary_get_uint64(message->content,"lookup-handle"))
        error=darwin_native_service_lookup(bootstrap,(uintptr_t)name,(uintptr_t)&port,0,0,
            spc_dictionary_get_uint64(message->content,"flags"));
    spc_dictionary_t *reply=spc_dictionary_create();
    spc_dictionary_set_int64(reply,"error",error);
    if (!error) spc_dictionary_set_send_port(reply,"port",port);
    spc_message_t response={.remote_port={header->msgh_remote_port,D_MACH_TYPE_MOVE_SEND_ONCE},
        .id=0x20000000,.content=reply};
    spc_mach_message_t *encoded=spc_serialize(&response);
    if (!encoded || encoded->header.msgh_size>capacity) abort();
    memcpy(output,encoded,encoded->header.msgh_size);
    fprintf(stderr,"BOOTSTRAP_XPC_REPLY error=%d port=%u bytes=%u\n",error,port,encoded->header.msgh_size);
    free(encoded);
    spc_dictionary_destroy(reply);
    *held_port=port; /* Keep lookup's send reference through the actual send. */
    spc_message_destroy(message);
    return 1;
}
