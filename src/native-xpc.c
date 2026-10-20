/* Host-TLS service thread only. Codec: pinned MIT libspc fork.
 * Connection/message rights remain in Darling/XNU. No guest libc is called. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <poll.h>
#include <errno.h>
#include "darwin-native.h"
#include "darwin-machine.h"
#include "datatypes.h"
#include "dictionary.h"
#include "array.h"
#include "serialization.h"
#include "accept.h"
#include "ios-scene-codec.h"

mach_port_t mach_task_self(void) { return darwin_native_host_task(); }
int mach_port_deallocate(mach_port_t task,mach_port_t port)
{ (void)task; return darwin_native_host_port_deallocate(port); }
int mach_port_addref(mach_port_t port,mach_port_right_t right)
{ return right==MACH_PORT_RIGHT_SEND ? darwin_native_host_port_refs(port,1) : D_KERN_NOT_SUPPORTED; }
int fileport_makeport(int fd,mach_port_t *port)
{ (void)fd; (void)port; return D_ENOTSUP; }

int arlinux_xpc_bootstrap(void *request,void *output,size_t capacity,uint32_t bootstrap,uint32_t *held_port,const uint32_t *audit)
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
     * against the requester's current audit PID/incarnation. The kernel audit
     * trailer identifies the requesting task across processes; never echo this
     * service's PID instead. See run 39385834761's interface-routine code. */
    uint32_t requester=0,incarnation=0;
    if (!audit) abort();
    requester=audit[5]; incarnation=audit[7];
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

/* Actual Wayland geometry belongs to the sender's audited process incarnation.
 * Retain a pidfd rather than trusting a PID supplied in the message body. */
static struct session_display {
    uint32_t pid,incarnation,width,height,refresh,backend;
    int pidfd,backend_pidfd;
    struct session_display *next;
} *session_displays;

int arlinux_session_display(void *request,unsigned long length,const uint32_t *audit)
{
    mach_msg_header_t *header=request;
    if (header->msgh_id!=D_SESSION_DISPLAY_REGISTER) return 0;
    const uint32_t *fields=(const uint32_t *)request+6;
    if (length!=40 || header->msgh_size!=40 || header->msgh_bits!=(D_MACH_TYPE_MOVE_SEND<<8) ||
        header->msgh_remote_port || header->msgh_voucher_port || !audit || !audit[5] ||
        !fields[0] || !fields[1] || fields[0]>8192 || fields[1]>8192 || !fields[2] ||
        !fields[3] || !darwin_native_host_child(fields[3],audit[5]) || !darwin_native_host_process_matches(audit[5],audit[7])) {
        fprintf(stderr,"SESSION_DISPLAY_INVALID\n"); abort();
    }
    for (struct session_display *display=session_displays;display;display=display->next) {
        if (display->pid==audit[5] && display->incarnation==audit[7]) {
            fprintf(stderr,"SESSION_DISPLAY_DUPLICATE pid=%u\n",audit[5]); abort();
        }
    }
    int fd=syscall(SYS_pidfd_open,audit[5],0),backend_fd=syscall(SYS_pidfd_open,fields[3],0);
    if (fd<0 || backend_fd<0 || !darwin_native_host_process_matches(audit[5],audit[7]) ||
        !darwin_native_host_child(fields[3],audit[5])) {
        if (fd>=0) close(fd);
        if (backend_fd>=0) close(backend_fd);
        fprintf(stderr,"SESSION_DISPLAY_SENDER_EXITED pid=%u\n",audit[5]);
        return 1;
    }
    struct session_display *display=calloc(1,sizeof(*display));
    if (!display) abort();
    display->pid=audit[5]; display->incarnation=audit[7]; display->pidfd=fd; display->backend_pidfd=backend_fd;
    display->width=fields[0]; display->height=fields[1]; display->refresh=fields[2]; display->backend=fields[3];
    display->next=session_displays; session_displays=display;
    fprintf(stderr,"SESSION_DISPLAY_REGISTERED pid=%u incarnation=%u width=%u height=%u refresh_millihz=%u backend=%u\n",
        display->pid,display->incarnation,display->width,display->height,display->refresh,display->backend);
    return 1;
}

/* Connection associations only; XNU owns the transferred rights and queues. */
static struct processinfo_peer {
    spc_connection_t connection;
    uint32_t pid,incarnation,task,endpoint,launch_reply,scene_reply;
    int pidfd,service,launch_state,scene_state;
    spc_message_t *workspace_registration;
    struct processinfo_peer *next;
} *processinfo_peers;
static const char *const session_service_names[D_SESSION_SERVICE_COUNT]={
    D_SESSION_PROCESSINFO_NAME,D_SESSION_WORKSPACE_NAME,D_SESSION_SYSTEMAPP_NAME,D_SESSION_HIDFOCUS_NAME};

static void session_peer_destroy(struct processinfo_peer *peer)
{
    if ((peer->task && darwin_native_host_port_deallocate(peer->task)) ||
        (peer->endpoint && darwin_native_host_port_deallocate(peer->endpoint)) ||
        (peer->launch_reply && darwin_native_host_receive_destroy(peer->launch_reply)) ||
        (peer->scene_reply && darwin_native_host_receive_destroy(peer->scene_reply)) ||
        darwin_native_host_port_deallocate(peer->connection.send_port) ||
        darwin_native_host_receive_destroy(peer->connection.receive_port)) abort();
    if (peer->pidfd>=0) close(peer->pidfd);
    if (peer->workspace_registration) spc_message_destroy(peer->workspace_registration);
    free(peer);
}

static void session_send_event(struct processinfo_peer *peer,uint32_t set,spc_dictionary_t *dictionary,
    uint32_t *reply,int *state,const char *label)
{
    if (darwin_native_host_receive_create(reply) || darwin_native_host_join(*reply,set)) abort();
    spc_message_t message={.remote_port={peer->connection.send_port,D_MACH_TYPE_COPY_SEND},
        .local_port={*reply,D_MACH_TYPE_MAKE_SEND_ONCE},.id=0x10000000,.content=dictionary};
    spc_mach_message_t *encoded=spc_serialize(&message);
    if (!encoded) abort();
    encoded->header.msgh_voucher_port=0;
    int status=darwin_native_host_send(encoded,encoded->header.msgh_size);
    free(encoded); spc_dictionary_destroy(dictionary);
    *state=status ? -1 : 1;
    fprintf(stderr,"%s pid=%u incarnation=%u status=%d reply=%u\n",
        label,peer->pid,peer->incarnation,status,*reply);
    if (status) {
        if (darwin_native_host_receive_destroy(*reply)) abort();
        *reply=0;
    }
}

static void session_send_launch(struct processinfo_peer *peer,uint32_t set)
{
    /* Actual dispatcher/codecs in run 39393971710; genuine send-once reply. */
    enum { FBS_UIAPPLICATION_LAUNCH=1 };
    spc_dictionary_t *event=spc_dictionary_create(),*dictionary=spc_dictionary_create();
    spc_dictionary_set_string(event,"bs_encodedObjectClassName","FBSUIApplicationLaunchEvent");
    spc_dictionary_set_value(dictionary,"event",(spc_value_t){.type=SPC_TYPE_DICT,.value.dict=event});
    spc_dictionary_set_int64(dictionary,"message",FBS_UIAPPLICATION_LAUNCH);
    session_send_event(peer,set,dictionary,&peer->launch_reply,&peer->launch_state,"SESSION_LAUNCH_SEND");
}

static void session_send_scene(struct processinfo_peer *peer,uint32_t set)
{
    /* Run 39395977685: queue_handleMessage's actual jump table maps 5 to
     * _queue_handleCreateScene:. The codecs validate specClass and read the
     * sceneID/settings/clientSettings/transitionContext fields. The codec
     * consumes only this peer's authenticated registered display geometry. */
    struct session_display *display=session_displays;
    while (display && (display->pid!=peer->pid || display->incarnation!=peer->incarnation)) display=display->next;
    if (!display) return; /* No invented fallback geometry before registration. */
    char identifier[80];
    snprintf(identifier,sizeof(identifier),"arlinux:%u:%u:main",peer->pid,peer->incarnation);
    struct ios_scene_geometry geometry={display->width,display->height,display->refresh};
    spc_dictionary_t *dictionary=ios_scene_create_event(identifier,&geometry);
    fprintf(stderr,"SESSION_SCENE_CONFIGURATION pid=%u scene=%s settings=class-display-frame-diffs lifecycle_action=activate width=%u height=%u refresh_millihz=%u geometry-established=false\n",
        peer->pid,identifier,display->width,display->height,display->refresh);
    session_send_event(peer,set,dictionary,&peer->scene_reply,&peer->scene_state,"SESSION_SCENE_SEND");
}

void arlinux_xpc_session_tick(uint32_t set)
{
    struct session_display **display_slot=&session_displays;
    while (*display_slot) {
        struct session_display *display=*display_slot;
        struct pollfd fds[2]={{.fd=display->pidfd,.events=POLLIN},{.fd=display->backend_pidfd,.events=POLLIN}};
        int ready=poll(fds,2,0);
        if (ready<0 && errno==EINTR) continue;
        if (ready<0 || (ready && !((fds[0].revents|fds[1].revents)&POLLIN))) abort();
        if (!ready) { display_slot=&display->next; continue; }
        fprintf(stderr,"SESSION_DISPLAY_RETIRED pid=%u incarnation=%u\n",display->pid,display->incarnation);
        *display_slot=display->next; close(display->pidfd); close(display->backend_pidfd); free(display);
    }

    struct processinfo_peer **slot=&processinfo_peers;
    while (*slot) {
        struct processinfo_peer *peer=*slot;
        struct pollfd fd={.fd=peer->pidfd,.events=POLLIN};
        int ready=poll(&fd,1,0);
        if (ready<0 && errno==EINTR) continue;
        if (ready<0) { perror("poll session client pidfd"); abort(); }
        if (!ready) {
            if (peer->workspace_registration && !peer->launch_state) session_send_launch(peer,set);
            if (peer->launch_state==2 && !peer->scene_state) session_send_scene(peer,set);
            slot=&peer->next; continue;
        }
        if (!(fd.revents&POLLIN)) { fprintf(stderr,"PROCESSINFO_PIDFD_ERROR events=%x\n",fd.revents); abort(); }
        fprintf(stderr,"PROCESSINFO_RETIRED pid=%u incarnation=%u service=%s\n",peer->pid,peer->incarnation,session_service_names[peer->service]);
        *slot=peer->next; session_peer_destroy(peer);
    }
}

int arlinux_xpc_session(void *request,size_t length,void *output,size_t capacity,const uint32_t *listeners,uint32_t set,const uint32_t *audit)
{
    mach_msg_header_t *header=request;
    unsigned int service=0;
    while (service<D_SESSION_SERVICE_COUNT &&
        (!listeners[service] || header->msgh_local_port!=listeners[service])) service++;
    if (service<D_SESSION_SERVICE_COUNT) {
        struct processinfo_peer *peer=calloc(1,sizeof(*peer));
        if (!peer || !spc_accept_message(request,length,&peer->connection)) {
            fprintf(stderr,"PROCESSINFO_CHECKIN_REJECTED id=%x bits=%x bytes=%zu\n",header->msgh_id,header->msgh_bits,length);
            abort();
        }
        if (!audit) abort();
        peer->service=service;
        peer->pid=audit[5]; peer->incarnation=audit[7];
        /* Hold a stable Linux process reference before checking its audit
         * incarnation. A recycled PID must not inherit this connection. */
        peer->pidfd=syscall(SYS_pidfd_open,peer->pid,0);
        if (peer->pidfd<0 && errno!=ESRCH) {
            fprintf(stderr,"PROCESSINFO_PEER_IDENTITY_UNAVAILABLE pid=%u incarnation=%u\n",peer->pid,peer->incarnation);
            abort();
        }
        if (peer->pidfd<0 || !darwin_native_host_process_matches(peer->pid,peer->incarnation)) {
            fprintf(stderr,"SESSION_XPC_STALE_CHECKIN pid=%u incarnation=%u service=%s\n",
                peer->pid,peer->incarnation,session_service_names[service]);
            session_peer_destroy(peer); return 0;
        }
        int error=darwin_native_host_join(peer->connection.receive_port,set);
        if (error) { fprintf(stderr,"PROCESSINFO_CHECKIN_JOIN_FAILED error=%d\n",error); abort(); }
        peer->next=processinfo_peers; processinfo_peers=peer;
        fprintf(stderr,"PROCESSINFO_CONNECTED receive=%u send=%u\n",peer->connection.receive_port,peer->connection.send_port);
        fprintf(stderr,"SESSION_XPC_CONNECTED service=%s pid=%u incarnation=%u receive=%u send=%u\n",
            session_service_names[service],peer->pid,peer->incarnation,peer->connection.receive_port,peer->connection.send_port);
        return 0;
    }
    struct processinfo_peer *peer=processinfo_peers;
    while (peer && peer->connection.receive_port!=header->msgh_local_port &&
        (!peer->launch_reply || peer->launch_reply!=header->msgh_local_port) &&
        (!peer->scene_reply || peer->scene_reply!=header->msgh_local_port)) peer=peer->next;
    if (!peer) return -1;
    int launch_reply=peer->launch_reply && peer->launch_reply==header->msgh_local_port;
    int scene_reply=peer->scene_reply && peer->scene_reply==header->msgh_local_port;
    uint32_t audit_bits=0;
    if (audit) for (unsigned int i=0;i<8;i++) audit_bits|=audit[i];
    /* XNU notify.defs: destroying an unused reply send-once right emits a
     * header-only MACH_NOTIFY_SEND_ONCE with KERNEL_AUDIT_TOKEN. This is a
     * cancellation, never an application reply or a successful scene. */
    if ((launch_reply || scene_reply) && audit && !audit_bits &&
        header->msgh_id==D_MACH_NOTIFY_SEND_ONCE && length==sizeof(*header) &&
        header->msgh_bits==(D_MACH_TYPE_MOVE_SEND_ONCE<<D_MACH_MSG_LOCAL_BITS_SHIFT) &&
        !header->msgh_remote_port && !header->msgh_voucher_port) {
        uint32_t *port=scene_reply ? &peer->scene_reply : &peer->launch_reply;
        int *state=scene_reply ? &peer->scene_state : &peer->launch_state;
        fprintf(stderr,"SESSION_EVENT_REPLY_CANCELLED pid=%u incarnation=%u event=%s port=%u\n",
            peer->pid,peer->incarnation,scene_reply ? "create-scene" : "launch",*port);
        if (darwin_native_host_receive_destroy(*port)) abort();
        *port=0; *state=-1; return 0;
    }
    if (!audit || audit[5]!=peer->pid || audit[7]!=peer->incarnation) {
        fprintf(stderr,"PROCESSINFO_PEER_IDENTITY_MISMATCH expected_pid=%u expected_incarnation=%u pid=%u incarnation=%u audit_bits=%x id=%x bits=%x bytes=%zu local=%u\n",
            peer->pid,peer->incarnation,audit ? audit[5] : 0,audit ? audit[7] : 0,
            audit_bits,header->msgh_id,header->msgh_bits,length,header->msgh_local_port); abort();
    }
    fprintf(stderr,"PROCESSINFO_MESSAGE id=%x bits=%x bytes=%zu reply=%u\n",header->msgh_id,header->msgh_bits,length,header->msgh_remote_port);
    fprintf(stderr,"SESSION_XPC_MESSAGE service=%s pid=%u incarnation=%u bytes=%zu\n",
        session_service_names[peer->service],peer->pid,peer->incarnation,length);
    /* Do not let upstream silently discard OOL resources. Actual process-state
     * protocol behavior is still being diagnosed, not acknowledged as success. */
    if (length<32 || header->msgh_id!=((launch_reply || scene_reply) ? 0x20000000 : 0x10000000)) {
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
    if (peer->service==D_SESSION_HIDFOCUS) {
        /* Diagnostic transport only: retain the authentic NSXPC invocation
         * before implementing focus behavior. Never reply with a fabricated
         * subscription, focus owner or successful invocation. */
        fprintf(stderr,"HIDFOCUS_REQUEST_UNIMPLEMENTED pid=%u incarnation=%u fields=%zu id=%x\n",
                peer->pid,peer->incarnation,message->content->num_items,header->msgh_id);
        fprintf(stdout,"HIDFOCUS_REQUEST_DICTIONARY pid=%u incarnation=%u\n",peer->pid,peer->incarnation);
        spc_dump(message->content); fflush(stdout); fflush(stderr);
        _exit(D_RUNTIME_FATAL_EXIT);
    }
    if (launch_reply || scene_reply) {
        const char *label=scene_reply ? "SESSION_SCENE_REPLY" : "SESSION_LAUNCH_REPLY";
        fprintf(stderr,"%s pid=%u incarnation=%u fields=%zu\n",label,peer->pid,peer->incarnation,message->content->num_items);
        fprintf(stdout,"%s_DICTIONARY pid=%u\n",label,peer->pid);
        spc_dump(message->content); fflush(stdout);
        spc_message_destroy(message);
        if (header->msgh_remote_port && darwin_native_host_port_deallocate(header->msgh_remote_port)) abort();
        uint32_t *port=scene_reply ? &peer->scene_reply : &peer->launch_reply;
        int *state=scene_reply ? &peer->scene_state : &peer->launch_state;
        if (darwin_native_host_receive_destroy(*port)) abort();
        *port=0; *state=2;
        return 0;
    }
    int error=D_ENOTSUP;
    /* Actual BKSWorkspace.init registration, observed in run 39391469924:
     * message 11 carries its listener endpoint and submitted task capability.
     * Retain both actual rights for this authenticated client lifetime. This
     * does not implement FrontBoard scene launch or invent process state. */
    spc_dictionary_item_t *kind=spc_dictionary_lookup(message->content,"message");
    spc_dictionary_item_t *task=spc_dictionary_lookup(message->content,"taskPort");
    spc_dictionary_item_t *endpoint=spc_dictionary_lookup(message->content,"endpoint");
    if (peer->service==D_SESSION_PROCESSINFO && kind && kind->value.type==SPC_TYPE_INT64 && kind->value.value.i64==11 &&
        message->content->num_items==3 && task && task->value.type==SPC_TYPE_SEND_PORT &&
        endpoint && endpoint->value.type==SPC_TYPE_ENDPOINT &&
        task->value.value.port.name && endpoint->value.value.port.name) {
        uint32_t new_task=task->value.value.port.name,new_endpoint=endpoint->value.value.port.name;
        if (darwin_native_host_port_refs(new_task,1) || darwin_native_host_port_refs(new_endpoint,1)) abort();
        if ((peer->task && darwin_native_host_port_deallocate(peer->task)) ||
            (peer->endpoint && darwin_native_host_port_deallocate(peer->endpoint))) abort();
        peer->task=new_task; peer->endpoint=new_endpoint; error=0;
        fprintf(stderr,"PROCESSINFO_WORKSPACE_REGISTERED pid=%u incarnation=%u task=%u endpoint=%u\n",
            peer->pid,peer->incarnation,peer->task,peer->endpoint);
    } else if (peer->service==D_SESSION_WORKSPACE && kind &&
        kind->value.type==SPC_TYPE_INT64 && !kind->value.value.i64) {
        spc_dictionary_item_t *event=spc_dictionary_lookup(message->content,"event");
        spc_dictionary_t *dictionary=event && event->value.type==SPC_TYPE_DICT ? event->value.value.dict : NULL;
        const char *class_name=dictionary ? spc_dictionary_get_string(dictionary,"bs_encodedObjectClassName") : NULL;
        spc_dictionary_item_t *handle=dictionary ? spc_dictionary_lookup(dictionary,"processHandle") : NULL;
        spc_dictionary_t *process=handle && handle->value.type==SPC_TYPE_DICT ? handle->value.value.dict : NULL;
        const char *handle_class=process ? spc_dictionary_get_string(process,"bs_encodedObjectClassName") : NULL;
        spc_dictionary_item_t *pid=process ? spc_dictionary_lookup(process,"pid") : NULL;
        /* The real connect event (run 39393138016) identifies this client.
         * Retain its metadata under the kernel-audited connection identity;
         * a supplied process handle cannot register a different process. */
        if (class_name && !strcmp(class_name,"FBSWorkspaceConnectEvent") &&
            handle_class && !strcmp(handle_class,"FBSProcessHandle") && pid &&
            (pid->value.type==SPC_TYPE_INT64 || pid->value.type==SPC_TYPE_UINT64) &&
            pid->value.value.u64==peer->pid &&
            spc_dictionary_get_string(process,"bundleID") && spc_dictionary_get_string(process,"bundlePath")) {
            if (peer->workspace_registration) spc_message_destroy(peer->workspace_registration);
            peer->workspace_registration=message; error=0;
            fprintf(stderr,"SESSION_WORKSPACE_ASSOCIATED pid=%u incarnation=%u\n",peer->pid,peer->incarnation);
        }
    }
    if (error) fprintf(stderr,"SESSION_XPC_REQUEST_UNSUPPORTED service=%s fields=%zu\n",
        session_service_names[peer->service],message->content->num_items);
    fprintf(stdout,"SESSION_XPC_DICTIONARY service=%s pid=%u\n",session_service_names[peer->service],peer->pid);
    spc_dump(message->content); fflush(stdout);
    int reply=header->msgh_remote_port!=MACH_PORT_NULL;
    if (reply && MACH_MSGH_BITS_REMOTE(header->msgh_bits)!=D_MACH_TYPE_MOVE_SEND_ONCE) {
        fprintf(stderr,"PROCESSINFO_REPLY_RIGHT_UNSUPPORTED\n"); abort();
    }
    if (reply) {
        spc_dictionary_t *dictionary=spc_dictionary_create();
        spc_dictionary_set_int64(dictionary,"error",error);
        spc_message_t response={.remote_port={header->msgh_remote_port,D_MACH_TYPE_MOVE_SEND_ONCE},
            .id=0x20000000,.content=dictionary};
        spc_mach_message_t *encoded=spc_serialize(&response);
        if (!encoded || encoded->header.msgh_size>capacity) abort();
        encoded->header.msgh_voucher_port=0;
        memcpy(output,encoded,encoded->header.msgh_size);
        free(encoded); spc_dictionary_destroy(dictionary);
    }
    if (peer->workspace_registration!=message) spc_message_destroy(message);
    return reply;
}
