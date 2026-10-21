/* Host-TLS service thread only. Codec: pinned MIT libspc fork.
 * Connection/message rights remain in Darling/XNU. No guest libc is called. */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <poll.h>
#include <errno.h>
#include <signal.h>
#include <sys/wait.h>
#include <time.h>
#include "native-service.h"
#include "darwin-native.h"
#include "darwin-machine.h"
#include "datatypes.h"
#include "dictionary.h"
#include "array.h"
#include "serialization.h"
#include "accept.h"

mach_port_t mach_task_self(void) { return darwin_native_host_task(); }
int mach_port_deallocate(mach_port_t task,mach_port_t port)
{ (void)task; return darwin_native_host_port_deallocate(port); }
int mach_port_addref(mach_port_t port,mach_port_right_t right)
{ return right==MACH_PORT_RIGHT_SEND ? darwin_native_host_port_refs(port,1) : D_KERN_NOT_SUPPORTED; }
int fileport_makeport(int fd,mach_port_t *port)
{ (void)fd; (void)port; return D_ENOTSUP; }

/* The kernel owns descriptor copyin/copyout. This receiver borrows the actual
 * delivered byte span while libspc constructs owned values, then releases OOL
 * mappings. No synthetic Mach message or extra full-payload copy is needed. */
static spc_message_t *session_deserialize(mach_msg_header_t *header,size_t length)
{
    if(length<sizeof(*header) || header->msgh_size!=length)return NULL;
    size_t offset=sizeof(*header),count=0,port_count=0,memory_count=0;
    if(header->msgh_bits&MACH_MSGH_BITS_COMPLEX) {
        if(length-offset<sizeof(mach_msg_body_t))return NULL;
        mach_msg_body_t body;memcpy(&body,(char *)header+offset,sizeof(body));
        offset+=sizeof(body);count=body.msgh_descriptor_count;
        if(count>(length-offset)/sizeof(mach_msg_port_descriptor_t))return NULL;
    }
    spc_port_t *ports=calloc(count ? count : 1,sizeof(*ports));
    mach_msg_ool_descriptor_t *memory=calloc(count ? count : 1,sizeof(*memory));
    if(!ports || !memory){free(ports);free(memory);return NULL;}
    int error=0;
    for(size_t i=0;i<count;i++) {
        if(length-offset<sizeof(mach_msg_type_descriptor_t)){error=EINVAL;break;}
        const unsigned char *descriptor=(const unsigned char *)header+offset;
        unsigned int type=descriptor[11];
        if(type==MACH_MSG_PORT_DESCRIPTOR) {
            mach_msg_port_descriptor_t port;memcpy(&port,descriptor,sizeof(port));
            if(port.disposition!=D_MACH_TYPE_MOVE_SEND && port.disposition!=D_MACH_TYPE_MOVE_SEND_ONCE) {
                fprintf(stderr,"XPC_PORT_DISPOSITION_UNSUPPORTED type=%u\n",port.disposition);
                abort(); /* Receive-right adoption is not implemented. */
            }
            ports[port_count++]=(spc_port_t){port.name,port.disposition};
            offset+=sizeof(port);
        } else if(type==MACH_MSG_OOL_DESCRIPTOR || type==D_MACH_MSG_OOL_VOLATILE_DESCRIPTOR) {
            if(length-offset<sizeof(*memory)){error=EINVAL;break;}
            memcpy(memory+memory_count,descriptor,sizeof(*memory));
            fprintf(stderr,"XPC_OOL_RECEIVED index=%zu bytes=%u copy=%u\n",i,memory[memory_count].size,memory[memory_count].copy);
            memory_count++;offset+=sizeof(*memory);
        } else {
            fprintf(stderr,"XPC_DESCRIPTOR_UNSUPPORTED type=%u\n",type);abort();
        }
    }
    const void *payload=(const unsigned char *)header+offset;
    size_t bytes=length-offset;
    if(!error && memory_count) {
        if(memory_count!=1 || bytes || memory[0].size>D_XPC_MAX_PAYLOAD_BYTES)error=ENOTSUP;
        else {payload=(const void *)(uintptr_t)memory[0].address;bytes=memory[0].size;}
    }
    spc_message_t *message=error ? NULL : spc_deserialize_payload(header,payload,bytes,ports,port_count,&error);
    /* The codec clears every adopted right, including on a decode failure. */
    for(size_t i=0;i<port_count;i++)if(ports[i].name)mach_port_deallocate(mach_task_self(),ports[i].name);
    for(size_t i=0;i<memory_count;i++)if(memory[i].size) {
        int status=darwin_native_host_memory_deallocate(memory[i].address,memory[i].size);
        fprintf(stderr,"XPC_OOL_RELEASED bytes=%u status=%d\n",memory[i].size,status);
        if(status)abort(); /* A real resource-ownership failure, not success. */
    }
    free(ports);free(memory);
    if(!message)fprintf(stderr,"XPC_DECODE_FAILED id=%x bytes=%zu error=%d\n",header->msgh_id,bytes,error);
    return message;
}
static int session_error_reply(mach_msg_header_t *header,void *output,size_t capacity,const uint32_t *audit)
{
    if(!header->msgh_remote_port)return 0;
    if(MACH_MSGH_BITS_REMOTE(header->msgh_bits)!=D_MACH_TYPE_MOVE_SEND_ONCE)abort();
    spc_dictionary_t *dictionary=spc_dictionary_create();
    spc_dictionary_set_int64(dictionary,"error",D_ENOTSUP);
    if(audit) {
        spc_dictionary_set_uint64(dictionary,"req_pid",audit[5]);
        spc_dictionary_set_uint64(dictionary,"rec_execcnt",audit[7]);
    }
    spc_message_t response={.remote_port={header->msgh_remote_port,D_MACH_TYPE_MOVE_SEND_ONCE},
        .id=0x20000000,.content=dictionary};
    spc_mach_message_t *encoded=spc_serialize(&response);
    if(!encoded || encoded->header.msgh_size>capacity)abort();
    encoded->header.msgh_voucher_port=0;
    memcpy(output,encoded,encoded->header.msgh_size);
    free(encoded);spc_dictionary_destroy(dictionary);
    return 1;
}

/* A service's bootstrap dictionary is launch configuration, not a connection
 * endpoint. Keep actual owned values/rights under the caller's audited lifetime;
 * no process, successful lookup or ready service is invented by storing it. */
static struct launch_bootstrap {
    uint32_t pid,incarnation;
    uint32_t creator[8];
    int pidfd;
    char *name;
    spc_dictionary_t *configuration;
    struct launch_bootstrap *next;
} *launch_bootstraps;
static unsigned int launch_bootstrap_count;
static void launch_instances_ready(struct launch_bootstrap *configuration);
static void launch_instances_tick(void);
static void launch_bootstrap_tick(void)
{
    struct launch_bootstrap **slot=&launch_bootstraps;
    while(*slot) {
        struct launch_bootstrap *entry=*slot;
        struct pollfd fd={.fd=entry->pidfd,.events=POLLIN};
        int ready=poll(&fd,1,0);
        if(ready<0 && errno==EINTR)continue;
        if(ready<0 || (ready && !(fd.revents&POLLIN)))abort();
        if(!ready){slot=&entry->next;continue;}
        fprintf(stderr,"SESSION_BOOTSTRAP_RETIRED pid=%u incarnation=%u service=%s\n",
            entry->pid,entry->incarnation,entry->name);
        *slot=entry->next;launch_bootstrap_count--;
        spc_dictionary_destroy(entry->configuration);close(entry->pidfd);free(entry->name);free(entry);
    }
}
static void launch_bootstrap_shape(spc_dictionary_t *dictionary)
{
    /* Names/types only. Environment values, raw bodies and credentials never
     * enter logs; the actual dictionary remains private in runtime memory. */
    unsigned int count=0;
    for(spc_dictionary_item_t *item=dictionary->items;item;item=item->next) {
        if(count++==32){fprintf(stderr,"SESSION_BOOTSTRAP_FIELDS_TRUNCATED limit=32\n");break;}
        fprintf(stderr,"SESSION_BOOTSTRAP_FIELD key=%.128s type=%x\n",item->key,item->value.type);
    }
}
static int launch_bootstrap_store(spc_dictionary_t *request,const char *name,const uint32_t *audit)
{
    spc_dictionary_item_t *data=spc_dictionary_lookup(request,"bootstrap");
    if(!audit || !audit[5] || !name || !*name || strlen(name)>=D_BOOTSTRAP_NAME_MAX ||
       spc_dictionary_get_uint64(request,"type")!=5 ||
       spc_dictionary_get_uint64(request,"handle")!=audit[5] ||
       !data || data->value.type!=SPC_TYPE_DICT || !data->value.value.dict)return D_EINVAL;
    launch_bootstrap_tick();
    struct launch_bootstrap *entry=launch_bootstraps;
    while(entry && !(entry->pid==audit[5] && entry->incarnation==audit[7] && !strcmp(entry->name,name)))entry=entry->next;
    if(!darwin_native_host_process_matches(audit[5],audit[7]))return D_ESRCH;
    if(!entry) {
        if(launch_bootstrap_count==32)return D_ENOMEM;
        entry=calloc(1,sizeof(*entry));if(!entry)return D_ENOMEM;
        entry->pidfd=syscall(SYS_pidfd_open,audit[5],0);
        entry->name=strdup(name);
        if(entry->pidfd<0 || !entry->name || !darwin_native_host_process_matches(audit[5],audit[7])) {
            if(entry->pidfd>=0)close(entry->pidfd);
            free(entry->name);free(entry);return D_ESRCH;
        }
        entry->pid=audit[5];entry->incarnation=audit[7];
        entry->next=launch_bootstraps;launch_bootstraps=entry;launch_bootstrap_count++;
    }
    if(entry->configuration)spc_dictionary_destroy(entry->configuration);
    entry->configuration=data->value.value.dict;
    memcpy(entry->creator,audit,sizeof(entry->creator));
    data->value=spc_null_create(); /* Transfer codec ownership exactly once. */
    fprintf(stderr,"SESSION_BOOTSTRAP_STORED pid=%u incarnation=%u service=%s fields=%zu\n",
        entry->pid,entry->incarnation,entry->name,entry->configuration->num_items);
    launch_bootstrap_shape(entry->configuration);
    launch_instances_ready(entry);
    return 0;
}

/* Instance UUIDs distinguish concurrent one-shot services of one application.
 * Pending/started/exited are execution states, never connection readiness. */
static struct service_instance {
    uint32_t owner,incarnation,serial;
    unsigned char uuid[16];
    char *name;
    int ownerfd,attempted,terminating;
    uint32_t endpoint;
    int owns_receive;
    plist_t configuration;
    uint64_t kill_after;
    struct native_service_child child;
    struct service_instance *next;
} *service_instances;
static unsigned int service_instance_count,service_instance_serial;
static void launch_endpoint_close(struct service_instance *instance)
{
    if(!instance->endpoint)return;
    if(instance->owns_receive && darwin_native_host_receive_destroy(instance->endpoint))abort();
    if(darwin_native_host_port_deallocate(instance->endpoint))abort();
    instance->endpoint=0;instance->owns_receive=0;
}
static void launch_instance_start(struct service_instance *instance)
{
    if(instance->attempted)return;
    instance->attempted=1;
    int error=native_service_spawn(instance->owner,instance->incarnation,instance->name,instance->uuid,&instance->child);
    if(error) {
        fprintf(stderr,"SESSION_SERVICE_SPAWN_FAILED owner=%u incarnation=%u service=%s instance=%u errno=%d\n",
            instance->owner,instance->incarnation,instance->name,instance->serial,error);
        launch_endpoint_close(instance);
    }
    else fprintf(stderr,"SESSION_SERVICE_STARTED owner=%u incarnation=%u service=%s instance=%u pid=%ld start_ticks=%llu ready=false\n",
        instance->owner,instance->incarnation,instance->name,instance->serial,(long)instance->child.pid,
        (unsigned long long)instance->child.start_ticks);
}
static void launch_instances_ready(struct launch_bootstrap *configuration)
{
    for(struct service_instance *instance=service_instances;instance;instance=instance->next)
        if(instance->owner==configuration->pid && instance->incarnation==configuration->incarnation &&
           !strcmp(instance->name,configuration->name))launch_instance_start(instance);
}
static uint64_t launch_clock_ms(void)
{
    struct timespec now;if(clock_gettime(CLOCK_MONOTONIC,&now))abort();
    return (uint64_t)now.tv_sec*1000+now.tv_nsec/1000000;
}
static void launch_instances_tick(void)
{
    struct service_instance **slot=&service_instances;
    while(*slot) {
        struct service_instance *instance=*slot;
        struct pollfd owner={.fd=instance->ownerfd,.events=POLLIN};
        int ready=poll(&owner,1,0);
        if(ready<0 && errno==EINTR)continue;
        if(ready<0 || (ready && !(owner.revents&POLLIN)))abort();
        if(instance->child.pid) {
            if(ready && !instance->terminating) {
                if(syscall(SYS_pidfd_send_signal,instance->child.pidfd,SIGTERM,NULL,0) && errno!=ESRCH)abort();
                instance->terminating=1;instance->kill_after=launch_clock_ms()+500;
            } else if(instance->terminating==1 && launch_clock_ms()>=instance->kill_after) {
                if(syscall(SYS_pidfd_send_signal,instance->child.pidfd,SIGKILL,NULL,0) && errno!=ESRCH)abort();
                instance->terminating=2;
            }
            int status=0;pid_t child=waitpid(instance->child.pid,&status,WNOHANG);
            if(child<0 && errno!=EINTR)abort();
            if(child>0) {
                fprintf(stderr,"SESSION_SERVICE_EXITED owner=%u incarnation=%u service=%s instance=%u pid=%ld start_ticks=%llu wait_status=%d owner_cleanup=%d\n",
                    instance->owner,instance->incarnation,instance->name,instance->serial,(long)child,
                    (unsigned long long)instance->child.start_ticks,status,instance->terminating!=0);
                close(instance->child.pidfd);instance->child.pidfd=-1;instance->child.pid=0;
                launch_endpoint_close(instance);
            }
        }
        if(ready && !instance->child.pid) {
            *slot=instance->next;service_instance_count--;
            launch_endpoint_close(instance);plist_free(instance->configuration);
            close(instance->ownerfd);free(instance->name);free(instance);continue;
        }
        slot=&instance->next;
    }
}
static int launch_instance_lookup(spc_dictionary_t *request,const char *name,const uint32_t *audit,uint32_t *port)
{
    spc_dictionary_item_t *uuid=spc_dictionary_lookup(request,"instance");
    if(!audit || !name || !*name || strlen(name)>=D_BOOTSTRAP_NAME_MAX ||
       spc_dictionary_get_uint64(request,"handle")!=audit[5] ||
       !uuid || uuid->value.type!=SPC_TYPE_UUID || !uuid->value.value.ptr ||
       !darwin_native_host_process_matches(audit[5],audit[7]))return D_EINVAL;
    launch_instances_tick();
    struct service_instance *instance=service_instances;
    while(instance && !(instance->owner==audit[5] && instance->incarnation==audit[7] &&
          !strcmp(instance->name,name) && !memcmp(instance->uuid,uuid->value.value.ptr,16)))instance=instance->next;
    if(!instance) {
        if(service_instance_count==64)return D_ENOMEM;
        instance=calloc(1,sizeof(*instance));if(!instance)return D_ENOMEM;
        instance->ownerfd=syscall(SYS_pidfd_open,audit[5],0);instance->name=strdup(name);
        if(instance->ownerfd<0 || !instance->name || !darwin_native_host_process_matches(audit[5],audit[7])) {
            if(instance->ownerfd>=0)close(instance->ownerfd);
            free(instance->name);free(instance);return D_ESRCH;
        }
        int status=native_service_metadata(audit[5],audit[7],name,&instance->configuration);
        if(!status)status=darwin_native_host_endpoint_create(&instance->endpoint);
        if(status) {
            fprintf(stderr,"SESSION_SERVICE_PENDING_FAILED owner=%u service=%s status=%d\n",audit[5],name,status);
            if(instance->configuration)plist_free(instance->configuration);
            close(instance->ownerfd);free(instance->name);free(instance);
            return status==ENOENT ? D_ENOENT : D_ENOTSUP;
        }
        instance->owns_receive=1;
        instance->owner=audit[5];instance->incarnation=audit[7];instance->serial=++service_instance_serial;
        memcpy(instance->uuid,uuid->value.value.ptr,16);instance->child.pidfd=-1;
        instance->next=service_instances;service_instances=instance;service_instance_count++;
        fprintf(stderr,"SESSION_SERVICE_PENDING owner=%u incarnation=%u service=%s instance=%u source_receive=%u\n",
            instance->owner,instance->incarnation,name,instance->serial,instance->endpoint);
    }
    for(struct launch_bootstrap *context=launch_bootstraps;context;context=context->next)
        if(context->pid==instance->owner && context->incarnation==instance->incarnation && !strcmp(context->name,name))
            launch_instance_start(instance);
    if(!instance->endpoint || darwin_native_host_port_refs(instance->endpoint,1))return D_ESRCH;
    *port=instance->endpoint; /* Separate send reference held through the actual lookup reply. */
    return 0;
}

/* Return the actual caller-supplied initialization dictionary only to a real
 * child owned by that caller/service instance. The creator is the original
 * kernel audit token, not an invented PID-1 credential or the manager's token. */
static struct launch_bootstrap *launch_bootstrap_for_child(spc_dictionary_t *request,const uint32_t *audit)
{
    if(!audit || spc_dictionary_get_uint64(request,"type")!=5 ||
       spc_dictionary_get_uint64(request,"handle")!=audit[5] ||
       !spc_dictionary_get_bool(request,"self") ||
       !darwin_native_host_process_matches(audit[5],audit[7]))return NULL;
    for(struct service_instance *child=service_instances;child;child=child->next) {
        if(child->child.pid!=(pid_t)audit[5] || (uint32_t)child->child.start_ticks!=audit[7])continue;
        for(struct launch_bootstrap *entry=launch_bootstraps;entry;entry=entry->next)
            if(entry->pid==child->owner && entry->incarnation==child->incarnation &&
               !strcmp(entry->name,child->name) &&
               darwin_native_host_process_matches(entry->pid,entry->incarnation))return entry;
    }
    return NULL;
}

/* Plist metadata has no Mach rights. Preserve its actual scalar/container
 * values; unsupported plist types fail rather than supplying a default. */
static int service_metadata_value(plist_t node,spc_value_t *output,unsigned int depth,unsigned int *budget)
{
    *output=spc_null_create();
    if(!node || depth>16 || !*budget)return D_E2BIG;
    --*budget;int error=0;
    switch(plist_get_node_type(node)) {
    case PLIST_STRING:
        output->type=SPC_TYPE_STRING;plist_get_string_val(node,&output->value.str);
        if(!output->value.str)error=D_ENOMEM;
        break;
    case PLIST_BOOLEAN: {
        uint8_t value=0;plist_get_bool_val(node,&value);
        output->type=SPC_TYPE_BOOL;output->value.u64=value;break;
    }
    case PLIST_UINT:
        output->type=SPC_TYPE_UINT64;plist_get_uint_val(node,&output->value.u64);break;
    case PLIST_REAL:
        output->type=SPC_TYPE_DOUBLE;plist_get_real_val(node,&output->value.dbl);break;
    case PLIST_DATA: {
        char *bytes=NULL;uint64_t length=0;plist_get_data_val(node,&bytes,&length);
        if(length>D_XPC_MAX_PAYLOAD_BYTES || (!bytes && length)){free(bytes);error=D_E2BIG;break;}
        output->type=SPC_TYPE_DATA;output->value.data=(spc_data_t){(unsigned char *)bytes,length};break;
    }
    case PLIST_ARRAY:
        output->type=SPC_TYPE_ARRAY;output->value.array=spc_array_create();
        if(!output->value.array){*output=spc_null_create();error=D_ENOMEM;break;}
        for(uint32_t i=0;i<plist_array_get_size(node);i++) {
            spc_value_t value;error=service_metadata_value(plist_array_get_item(node,i),&value,depth+1,budget);
            if(error)break;
            spc_array_set_value(output->value.array,i,value);
        }
        break;
    case PLIST_DICT: {
        output->type=SPC_TYPE_DICT;output->value.dict=spc_dictionary_create();
        if(!output->value.dict){*output=spc_null_create();error=D_ENOMEM;break;}
        plist_dict_iter iterator=NULL;plist_dict_new_iter(node,&iterator);
        if(!iterator){error=D_ENOMEM;break;}
        for(;;) {
            char *key=NULL;plist_t child=NULL;plist_dict_next_item(node,iterator,&key,&child);
            if(!key)break;
            spc_value_t value;error=service_metadata_value(child,&value,depth+1,budget);
            if(!error)spc_dictionary_set_value(output->value.dict,key,value);
            free(key);if(error)break;
        }
        free(iterator);break;
    }
    default:error=D_ENOTSUP;break;
    }
    if(error){spc_value_destroy(*output);*output=spc_null_create();}
    return error;
}
static int service_metadata_reply(spc_dictionary_t *request,const uint32_t *audit,spc_dictionary_t *reply)
{
    if(!audit || spc_dictionary_get_uint64(request,"type")!=6 || !spc_dictionary_get_bool(request,"self") ||
       !darwin_native_host_process_matches(audit[5],audit[7]))return D_EINVAL;
    struct service_instance *child=service_instances;
    while(child && !(child->child.pid==(pid_t)audit[5] && (uint32_t)child->child.start_ticks==audit[7]))child=child->next;
    if(!child)return D_ESRCH;
    if(!darwin_native_host_process_matches(child->owner,child->incarnation))return D_ESRCH;
    unsigned int budget=1024;spc_value_t value;
    int error=service_metadata_value(child->configuration,&value,0,&budget);
    if(error)return error;
    spc_dictionary_set_string(reply,"identifier",child->name);
    spc_dictionary_set_value(reply,"xpcservice-dict",value);
    fprintf(stderr,"SESSION_SERVICE_METADATA_REPLY pid=%u incarnation=%u service=%s fields=%zu\n",
        audit[5],audit[7],child->name,value.value.dict->num_items);
    return 0;
}
static int service_checkin_reply(spc_dictionary_t *request,const uint32_t *audit,spc_dictionary_t *reply,
                                 struct service_instance **grant)
{
    const char *name=spc_dictionary_get_string(request,"name");
    if(!audit || !name || spc_dictionary_get_uint64(request,"type")!=6 ||
       spc_dictionary_get_uint64(request,"flags") || spc_dictionary_get_uint64(request,"handle") ||
       !darwin_native_host_process_matches(audit[5],audit[7]))return D_EINVAL;
    struct service_instance *child=service_instances;
    while(child && !(child->child.pid==(pid_t)audit[5] && (uint32_t)child->child.start_ticks==audit[7] &&
          !strcmp(child->name,name)))child=child->next;
    if(!child || !darwin_native_host_process_matches(child->owner,child->incarnation))return D_ESRCH;
    if(!child->endpoint || !child->owns_receive)return D_EBUSY;
    void *uuid=malloc(sizeof(child->uuid));if(!uuid)return D_ENOMEM;
    memcpy(uuid,child->uuid,sizeof(child->uuid));
    spc_dictionary_set_value(reply,"instance",(spc_value_t){.type=SPC_TYPE_UUID,.value.ptr=uuid});
    /* The receive right is unique. Borrow it through serialization; the real
     * Mach send transfers it, without manufacturing another receive reference. */
    spc_dictionary_set_value(reply,"port",(spc_value_t){.type=SPC_TYPE_RECV_PORT,
        .value.port={child->endpoint,D_MACH_TYPE_MOVE_RECEIVE}});
    *grant=child;return 0;
}

int arlinux_xpc_bootstrap(void *request,void *output,size_t capacity,uint32_t bootstrap,uint32_t *held_port,const uint32_t *audit)
{
    mach_msg_header_t *header=request;
    int packed_routine=((uint32_t)header->msgh_id&0xffff0000U)==0x40000000U;
    if (header->msgh_id!=0x10000000 && !packed_routine) return 0;
    spc_message_t *message=session_deserialize(header,header->msgh_size);
    if(!message)return session_error_reply(header,output,capacity,audit);
    uint64_t subsystem=spc_dictionary_get_uint64(message->content,"subsystem");
    uint64_t routine=spc_dictionary_get_uint64(message->content,"routine");
    const char *name=spc_dictionary_get_string(message->content,"name");
    fprintf(stderr,"BOOTSTRAP_XPC_REQUEST subsystem=%llu routine=%llu name=%s\n",
        (unsigned long long)subsystem,(unsigned long long)routine,name ? name : "<absent>");
    for (spc_dictionary_item_t *item=message->content->items;item;item=item->next) {
        if (item->value.type==SPC_TYPE_UINT64 || item->value.type==SPC_TYPE_INT64)
            fprintf(stderr,"BOOTSTRAP_XPC_FIELD key=%.128s type=%x value=%llu\n",item->key,item->value.type,
                (unsigned long long)item->value.value.u64);
        else if(item->value.type==SPC_TYPE_STRING &&
                (!strcmp(item->key,"name") || !strcmp(item->key,"path") || !strcmp(item->key,"origin") || !strcmp(item->key,"identifier")))
            fprintf(stderr,"BOOTSTRAP_XPC_FIELD key=%.128s type=%x value=%.512s\n",item->key,item->value.type,item->value.value.str);
        else fprintf(stderr,"BOOTSTRAP_XPC_FIELD key=%.128s type=%x\n",item->key,item->value.type);
    }
    int set_bootstrap=packed_routine && ((uint32_t)header->msgh_id&0xffffU)==0x2c6U;
    int copy_bootstrap=packed_routine && ((uint32_t)header->msgh_id&0xffffU)==0x2c7U;
    int copy_service=packed_routine && ((uint32_t)header->msgh_id&0xffffU)==0x2ceU;
    int check_in=packed_routine && ((uint32_t)header->msgh_id&0xffffU)==0x325U;
    if (packed_routine && ((uint32_t)header->msgh_id&0xffffU)!=0x324U && !set_bootstrap && !copy_bootstrap && !copy_service && !check_in) {
        /* Unknown interface routines keep the actual unsupported response. */
        fprintf(stderr,"BOOTSTRAP_XPC_PACKED_UNIMPLEMENTED id=%x bytes=%u\n",header->msgh_id,header->msgh_size);
        /* Scalar routing fields are logged above; don't dump arbitrary OOL
         * payloads or nested environment dictionaries. */
        spc_message_destroy(message);return 0;
    }
    int error=D_ENOTSUP;
    uint32_t port=0;
    struct launch_bootstrap *context=copy_bootstrap ? launch_bootstrap_for_child(message->content,audit) : NULL;
    if(copy_bootstrap)error=context ? 0 : D_ESRCH;
    if(set_bootstrap)error=launch_bootstrap_store(message->content,name,audit);
    if(packed_routine && ((uint32_t)header->msgh_id&0xffffU)==0x324U &&
       spc_dictionary_get_uint64(message->content,"type")==5 &&
       spc_dictionary_get_uint64(message->content,"flags")==32)
        error=launch_instance_lookup(message->content,name,audit,&port);
    /* libspc connection.c: spc_domain_routine / spc_look_up_endpoint.
     * Only this process's default namespace is currently implemented. */
    /* Real __xpc_pipe_pack_message emits packed routine IDs but still uses
     * 0x20000000 for dictionary replies (run 39491337714). The dictionary
     * format and actual rights/audit ownership are the same as legacy XPC. */
    if (((packed_routine && ((uint32_t)header->msgh_id&0xffffU)==0x324U) ||
         (!packed_routine && subsystem==3 && routine==0x324)) && name &&
        !spc_dictionary_get_uint64(message->content,"handle") &&
        !spc_dictionary_get_uint64(message->content,"lookup-handle")) {
        uint64_t type=spc_dictionary_get_uint64(message->content,"type");
        uint64_t flags=spc_dictionary_get_uint64(message->content,"flags");
        if (type==7 && !flags) error=darwin_native_service_lookup(bootstrap,(uintptr_t)name,(uintptr_t)&port,0,0,0);
        else error=darwin_native_system_lookup(name,type,flags,&port);
    }
    spc_dictionary_t *reply=spc_dictionary_create();
    struct service_instance *grant=NULL;
    if(copy_service)error=service_metadata_reply(message->content,audit,reply);
    if(check_in)error=service_checkin_reply(message->content,audit,reply,&grant);
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
    spc_dictionary_item_t *borrowed=NULL;
    if(context) {
        spc_dictionary_set_value(reply,"bootstrap",(spc_value_t){.type=SPC_TYPE_DICT,.value.dict=context->configuration});
        borrowed=spc_dictionary_lookup(reply,"bootstrap");
        spc_dictionary_set_data(reply,"creator",context->creator,sizeof(context->creator));
    }
    if (!error && !set_bootstrap && !copy_bootstrap && !copy_service && !check_in) spc_dictionary_set_send_port(reply,"port",port);
    spc_message_t response={.remote_port={header->msgh_remote_port,D_MACH_TYPE_MOVE_SEND_ONCE},
        .id=0x20000000,.content=reply};
    spc_mach_message_t *encoded=spc_serialize(&response);
    if(borrowed)borrowed->value=spc_null_create(); /* Context keeps its original owned objects/rights. */
    if(grant)spc_dictionary_lookup(reply,"port")->value=spc_null_create();
    int unsupported_copy=0;
    if(context && encoded && (encoded->header.msgh_bits&MACH_MSGH_BITS_COMPLEX)) {
        mach_msg_body_t *body=(void *)((char *)encoded+sizeof(mach_msg_header_t));
        mach_msg_port_descriptor_t *ports=(void *)(body+1);
        for(uint32_t i=0;i<body->msgh_descriptor_count;i++) {
            if(ports[i].type!=MACH_MSG_PORT_DESCRIPTOR ||
               (ports[i].disposition!=D_MACH_TYPE_MOVE_SEND && ports[i].disposition!=D_MACH_TYPE_COPY_SEND)) {
                unsupported_copy=1;break;
            }
            ports[i].disposition=D_MACH_TYPE_COPY_SEND;
        }
    }
    if((context || copy_service || check_in) && (!encoded || encoded->header.msgh_size>capacity || unsupported_copy)) {
        free(encoded);spc_dictionary_destroy(reply);spc_message_destroy(message);
        return session_error_reply(header,output,capacity,audit);
    }
    if (!encoded || encoded->header.msgh_size>capacity) abort();
    /* Upstream's malloc-backed header does not initialize the voucher field.
     * This host service has no voucher to transfer. */
    encoded->header.msgh_voucher_port=MACH_PORT_NULL;
    memcpy(output,encoded,encoded->header.msgh_size);
    if(grant) {
        /* The caller sends this reply immediately and invalidates the whole
         * session on send failure. There is no intervening actor mutation. */
        grant->owns_receive=0;
        fprintf(stderr,"SESSION_SERVICE_RECEIVE_GRANT pid=%u incarnation=%u service=%s instance=%u source_receive=%u\n",
            audit[5],audit[7],grant->name,grant->serial,grant->endpoint);
    }
    if(context)fprintf(stderr,"SESSION_BOOTSTRAP_COPY_REPLY pid=%u incarnation=%u creator=%u service=%s\n",
        audit[5],audit[7],context->pid,context->name);
    fprintf(stderr,"BOOTSTRAP_XPC_REPLY error=%d port=%u bytes=%u receive_grant=%d\n",
        error,grant ? grant->endpoint : port,encoded->header.msgh_size,grant!=NULL);
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
    uint32_t pid,incarnation,task,endpoint;
    int pidfd,service;
    struct processinfo_peer *next;
} *processinfo_peers;
static const char *const session_service_names[D_SESSION_SERVICE_COUNT]={
    D_SESSION_PROCESSINFO_NAME,D_SESSION_WORKSPACE_NAME,D_SESSION_SYSTEMAPP_NAME,D_SESSION_HIDFOCUS_NAME};

static void session_peer_destroy(struct processinfo_peer *peer)
{
    if ((peer->task && darwin_native_host_port_deallocate(peer->task)) ||
        (peer->endpoint && darwin_native_host_port_deallocate(peer->endpoint)) ||
        darwin_native_host_port_deallocate(peer->connection.send_port) ||
        darwin_native_host_receive_destroy(peer->connection.receive_port)) abort();
    if (peer->pidfd>=0) close(peer->pidfd);
    free(peer);
}

void arlinux_xpc_session_tick(void)
{
    launch_instances_tick();
    launch_bootstrap_tick();
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
    while (peer && peer->connection.receive_port!=header->msgh_local_port) peer=peer->next;
    if (!peer) return -1;
    uint32_t audit_bits=0;
    if (audit) for (unsigned int i=0;i<8;i++) audit_bits|=audit[i];
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
    /* __xpc_pipe_pack_message also emits the routine-0 request envelope on
     * established connections. Its CPX5 dictionary/rights contract is the
     * same; accepting the envelope does not implement its service operation. */
    if (length<32 || (header->msgh_id!=0x10000000 && header->msgh_id!=0x40000000)) {
        fprintf(stderr,"PROCESSINFO_MESSAGE_UNSUPPORTED\n"); abort();
    }
    spc_message_t *message=session_deserialize(header,length);
    if(!message)return session_error_reply(header,output,capacity,NULL);
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
    }
    /* Private workspace/system-app lifecycle operations are deliberately not
     * emulated here. The guest platform adapter will own the host-backed
     * lifecycle boundary; until then these requests remain unsupported. */
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
    spc_message_destroy(message);
    return reply;
}
