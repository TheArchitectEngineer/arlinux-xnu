/* Normal-call platform entries execute under guest TLS: only raw Linux calls,
 * checked guest copies and atomics, never host libc. The host pthread starts
 * before guest entry and alone owns libcurl and response allocations. */
#pragma once
#include "native-http.h"
static int native_http_text(char *output,uint64_t input,unsigned int capacity)
{
    for(unsigned int i=0;i<capacity;i++) {
        if(guest_copy(output+i,input+i,1,0))return -EFAULT;
        if(!output[i])return 0;
    }
    return -E2BIG;
}
static long native_http_submit(const struct arlinux_http_request *input)
{
    if(native_http_event<0)return -ENOTSUP;
    struct arlinux_http_request request;
    if(guest_copy(&request,(uintptr_t)input,sizeof(request),0))return -EFAULT;
    if(request.reserved || request.body_size>ARLINUX_HTTP_BODY_LIMIT ||
       !request.timeout_ms || request.timeout_ms>300000)return -EINVAL;
    unsigned int i;
    for(i=0;i<ARLINUX_HTTP_SLOTS;i++) {
        uint32_t expected=HTTP_FREE;
        if(__atomic_compare_exchange_n(&native_http_slots[i].state,&expected,HTTP_CLAIMED,0,
                                      __ATOMIC_ACQUIRE,__ATOMIC_RELAXED))break;
    }
    if(i==ARLINUX_HTTP_SLOTS)return -EAGAIN;
    struct native_http_slot *slot=native_http_slots+i;
    int error=native_http_text(slot->url,(uintptr_t)request.url,sizeof(slot->url));
    if(!error)error=native_http_text(slot->method,(uintptr_t)request.method,sizeof(slot->method));
    if(!error)error=native_http_text(slot->headers,(uintptr_t)request.headers,sizeof(slot->headers));
    slot->request_body=NULL;slot->request_size=request.body_size;
    if(!error && request.body_size) {
        long body=RAW(SYS_mmap,0,request.body_size,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
        if((unsigned long)body>=(unsigned long)-4095)error=(int)body;
        else {
            slot->request_body=(void *)body;
            error=guest_copy(slot->request_body,(uintptr_t)request.body,request.body_size,0);
        }
    }
    struct timespec now;
    if(!error)error=RAW(SYS_clock_gettime,CLOCK_MONOTONIC,&now,0,0,0,0);
    if(error) {
        if(slot->request_body)RAW(SYS_munmap,slot->request_body,slot->request_size,0,0,0,0);
        __atomic_store_n(&slot->state,HTTP_FREE,__ATOMIC_RELEASE);return error;
    }
    slot->deadline_ns=(uint64_t)now.tv_sec*1000000000+now.tv_nsec+(uint64_t)request.timeout_ms*1000000;
    slot->generation=(slot->generation+1)&0x7fffffffU;
    if(!slot->generation)slot->generation=1;
    slot->response=(struct arlinux_http_response){0};
    __atomic_store_n(&slot->cancelled,0,__ATOMIC_RELAXED);
    __atomic_store_n(&slot->released,0,__ATOMIC_RELAXED);
    long token=((uint64_t)slot->generation<<8)|(i+1);
    __atomic_store_n(&slot->state,HTTP_PENDING,__ATOMIC_RELEASE);
    uint64_t wake=1;
    RAW(SYS_write,native_http_event,&wake,sizeof(wake),0,0,0);
    return token;
}
static struct native_http_slot *native_http_find(uint64_t token)
{
    unsigned int index=token&255;
    if(!index || index>ARLINUX_HTTP_SLOTS)return NULL;
    struct native_http_slot *slot=native_http_slots+index-1;
    uint32_t state=__atomic_load_n(&slot->state,__ATOMIC_ACQUIRE);
    return state>=HTTP_PENDING && slot->generation==(token>>8) ? slot : NULL;
}
static long native_http_result(uint64_t token,struct arlinux_http_response *response)
{
    struct native_http_slot *slot=native_http_find(token);
    if(!slot)return -EINVAL;
    if(__atomic_load_n(&slot->state,__ATOMIC_ACQUIRE)!=HTTP_DONE)return 0;
    if(guest_copy(&slot->response,(uintptr_t)response,sizeof(*response),1))return -EFAULT;
    return 1;
}
static long native_http_release(uint64_t token)
{
    struct native_http_slot *slot=native_http_find(token);
    if(!slot)return -EINVAL;
    __atomic_store_n(&slot->cancelled,1,__ATOMIC_RELEASE);
    __atomic_store_n(&slot->released,1,__ATOMIC_RELEASE);
    uint64_t wake=1;RAW(SYS_write,native_http_event,&wake,sizeof(wake),0,0,0);
    return 0;
}
