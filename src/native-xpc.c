/* Host-TLS service thread only. Codec: pinned, unmodified MIT libspc.
 * Connection/message rights remain in Darling/XNU. No guest libc is called. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "darwin-native.h"
#include "datatypes.h"
#include "dictionary.h"
#include "serialization.h"
#include "accept.h"

mach_port_t mach_task_self(void) { return darwin_native_host_task(); }
int mach_port_deallocate(mach_port_t task,mach_port_t port)
{ (void)task; return darwin_native_host_port_deallocate(port); }
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
        !spc_dictionary_get_uint64(message->content,"lookup-handle")) {
        uint64_t type=spc_dictionary_get_uint64(message->content,"type");
        uint64_t flags=spc_dictionary_get_uint64(message->content,"flags");
        if (type==7) error=darwin_native_service_lookup(bootstrap,(uintptr_t)name,(uintptr_t)&port,0,0,flags);
        else error=darwin_native_system_lookup(name,type,flags,&port);
    }
    spc_dictionary_t *reply=spc_dictionary_create();
    /* The genuine non-PID-1 bootstrap path authenticates its requester echo
     * against the requester's current audit PID/incarnation. Host provider and
     * guest share this task; use the same Linux-backed identity as XNU, not a
     * made-up PID 1 or root token. See run 39385834761's interface-routine code. */
    uint32_t requester=0,incarnation=0;
    if (darwin_native_host_identity(&requester,&incarnation)) abort();
    spc_dictionary_set_uint64(reply,"req_pid",requester);
    spc_dictionary_set_uint64(reply,"rec_execcnt",incarnation);
    fprintf(stderr,"BOOTSTRAP_XPC_IDENTITY pid=%u incarnation=%u\n",requester,incarnation);
    spc_dictionary_set_int64(reply,"error",error);
    if (!error) spc_dictionary_set_send_port(reply,"port",port);
    spc_message_t response={.remote_port={header->msgh_remote_port,D_MACH_TYPE_MOVE_SEND_ONCE},
        .id=0x20000000,.content=reply};
    spc_mach_message_t *encoded=spc_serialize(&response);
    if (!encoded || encoded->header.msgh_size>capacity) abort();
    /* Upstream's malloc-backed header does not initialize the voucher field.
     * This host service has no voucher to transfer. */
    encoded->header.msgh_voucher_port=MACH_PORT_NULL;
    memcpy(output,encoded,encoded->header.msgh_size);
    fprintf(stderr,"BOOTSTRAP_XPC_REPLY error=%d port=%u bytes=%u\n",error,port,encoded->header.msgh_size);
    free(encoded);
    spc_dictionary_destroy(reply);
    *held_port=port; /* Keep lookup's send reference through the actual send. */
    spc_message_destroy(message);
    return 1;
}

/* Connection associations only; XNU owns the transferred rights and queues. */
static struct processinfo_peer {
    spc_connection_t connection;
    struct processinfo_peer *next;
} *processinfo_peers;

int arlinux_xpc_processinfo(void *request,size_t length,void *output,size_t capacity,uint32_t listener,uint32_t set)
{
    mach_msg_header_t *header=request;
    if (header->msgh_local_port==listener) {
        struct processinfo_peer *peer=calloc(1,sizeof(*peer));
        if (!peer || !spc_accept_message(request,length,&peer->connection)) {
            fprintf(stderr,"PROCESSINFO_CHECKIN_REJECTED id=%x bits=%x bytes=%zu\n",header->msgh_id,header->msgh_bits,length);
            abort();
        }
        int error=darwin_native_host_join(peer->connection.receive_port,set);
        if (error) { fprintf(stderr,"PROCESSINFO_CHECKIN_JOIN_FAILED error=%d\n",error); abort(); }
        peer->next=processinfo_peers; processinfo_peers=peer;
        fprintf(stderr,"PROCESSINFO_CONNECTED receive=%u send=%u\n",peer->connection.receive_port,peer->connection.send_port);
        return 0;
    }
    struct processinfo_peer *peer=processinfo_peers;
    while (peer && peer->connection.receive_port!=header->msgh_local_port) peer=peer->next;
    if (!peer) return -1;
    fprintf(stderr,"PROCESSINFO_MESSAGE id=%x bits=%x bytes=%zu reply=%u\n",header->msgh_id,header->msgh_bits,length,header->msgh_remote_port);
    /* Do not let upstream silently discard OOL resources. Actual process-state
     * protocol behavior is still being diagnosed, not acknowledged as success. */
    if (length<32 || header->msgh_id!=0x10000000) {
        fprintf(stderr,"PROCESSINFO_MESSAGE_UNSUPPORTED\n"); abort();
    }
    if (header->msgh_bits&MACH_MSGH_BITS_COMPLEX) {
        uint32_t count; memcpy(&count,(unsigned char *)request+sizeof(*header),sizeof(count));
        size_t offset=sizeof(*header)+sizeof(mach_msg_body_t);
        if (count>(length-offset)/sizeof(mach_msg_port_descriptor_t)) abort();
        for (uint32_t i=0;i<count;i++,offset+=sizeof(mach_msg_port_descriptor_t)) {
            mach_msg_port_descriptor_t descriptor;
            memcpy(&descriptor,(unsigned char *)request+offset,sizeof(descriptor));
            fprintf(stderr,"PROCESSINFO_DESCRIPTOR index=%u type=%u disposition=%u port=%u\n",
                i,descriptor.type,descriptor.disposition,descriptor.name);
            if (descriptor.type!=MACH_MSG_PORT_DESCRIPTOR ||
                (descriptor.disposition!=D_MACH_TYPE_MOVE_SEND &&
                 descriptor.disposition!=D_MACH_TYPE_MOVE_SEND_ONCE)) {
                fprintf(stderr,"PROCESSINFO_DESCRIPTOR_UNSUPPORTED\n"); abort();
            }
        }
    }
    spc_message_t *message=spc_deserialize(request);
    if (!message) abort();
    fprintf(stderr,"PROCESSINFO_REQUEST_UNSUPPORTED fields=%zu\n",message->content->num_items);
    spc_dump(message->content); fflush(stdout);
    int reply=header->msgh_remote_port!=MACH_PORT_NULL;
    if (reply && MACH_MSGH_BITS_REMOTE(header->msgh_bits)!=D_MACH_TYPE_MOVE_SEND_ONCE) {
        fprintf(stderr,"PROCESSINFO_REPLY_RIGHT_UNSUPPORTED\n"); abort();
    }
    if (reply) {
        spc_dictionary_t *dictionary=spc_dictionary_create();
        spc_dictionary_set_int64(dictionary,"error",D_ENOTSUP);
        spc_message_t response={.remote_port={header->msgh_remote_port,D_MACH_TYPE_MOVE_SEND_ONCE},
            .id=0x20000000,.content=dictionary};
        spc_mach_message_t *encoded=spc_serialize(&response);
        if (!encoded || encoded->header.msgh_size>capacity) abort();
        encoded->header.msgh_voucher_port=0;
        memcpy(output,encoded,encoded->header.msgh_size);
        free(encoded); spc_dictionary_destroy(dictionary);
    }
    spc_message_destroy(message);
    return reply;
}
