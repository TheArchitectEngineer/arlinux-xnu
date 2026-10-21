/* Host TLS and libcurl stay on this ordinary Linux pthread. Requests contain
 * copied bytes; responses stay immutable until the guest explicitly releases. */
#define _GNU_SOURCE
#include "native-http.h"
#include <curl/curl.h>
#include <pthread.h>
#include <sys/eventfd.h>
#include <sys/mman.h>
#include <poll.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <errno.h>
struct native_http_slot native_http_slots[ARLINUX_HTTP_SLOTS];
int native_http_event=-1;
struct http_buffer { char *bytes;size_t size,limit;int overflow; };
static size_t append_bytes(char *bytes,size_t size,size_t count,void *context)
{
    struct http_buffer *buffer=context;
    if(count && size>SIZE_MAX/count)return 0;
    size_t n=size*count;
    if(n>buffer->limit-buffer->size){buffer->overflow=1;return 0;}
    char *next=realloc(buffer->bytes,buffer->size+n+1);
    if(!next)return 0;
    memcpy(next+buffer->size,bytes,n);buffer->size+=n;next[buffer->size]=0;
    buffer->bytes=next;return n;
}
static size_t response_header(char *bytes,size_t size,size_t count,void *context)
{
    struct http_buffer *buffer=context;
    /* Interim 100 responses are not the final response headers. */
    if(size==1 && count>=5 && !memcmp(bytes,"HTTP/",5))buffer->size=0;
    return append_bytes(bytes,size,count,context);
}
static int cancelled(void *context,curl_off_t a,curl_off_t b,curl_off_t c,curl_off_t d)
{
    (void)a;(void)b;(void)c;(void)d;
    struct native_http_slot *slot=context;
    return __atomic_load_n(&slot->cancelled,__ATOMIC_ACQUIRE)!=0;
}
static uint32_t transport_error(CURLcode code)
{
    switch(code) {
    case CURLE_OK:return AR_HTTP_OK;
    case CURLE_OPERATION_TIMEDOUT:return AR_HTTP_TIMEOUT;
    case CURLE_COULDNT_RESOLVE_HOST:case CURLE_COULDNT_RESOLVE_PROXY:return AR_HTTP_DNS;
    case CURLE_COULDNT_CONNECT:return AR_HTTP_CONNECT;
    case CURLE_PEER_FAILED_VERIFICATION:case CURLE_SSL_CONNECT_ERROR:case CURLE_SSL_CERTPROBLEM:return AR_HTTP_TLS;
    case CURLE_ABORTED_BY_CALLBACK:return AR_HTTP_CANCELLED;
    case CURLE_URL_MALFORMAT:case CURLE_UNSUPPORTED_PROTOCOL:return AR_HTTP_INVALID;
    default:return AR_HTTP_IO;
    }
}
static void transfer(struct native_http_slot *slot)
{
    struct http_buffer body={.limit=ARLINUX_HTTP_BODY_LIMIT},headers={.limit=ARLINUX_HTTP_HEADER_LIMIT};
    CURL *curl=curl_easy_init();CURLcode code=CURLE_FAILED_INIT;
    struct curl_slist *fields=NULL;
    char *start=slot->headers;
    while(*start) {
        char *end=strstr(start,"\r\n");
        if(!end){code=CURLE_BAD_FUNCTION_ARGUMENT;goto done;}
        *end=0;
        struct curl_slist *next=curl_slist_append(fields,start);
        if(!next){code=CURLE_OUT_OF_MEMORY;goto done;}
        fields=next;start=end+2;
    }
    if(!curl)goto done;
    struct timespec now;clock_gettime(CLOCK_MONOTONIC,&now);
    uint64_t ns=(uint64_t)now.tv_sec*1000000000+now.tv_nsec;
    if(ns>=slot->deadline_ns){code=CURLE_OPERATION_TIMEDOUT;goto done;}
    if(cancelled(slot,0,0,0,0)){code=CURLE_ABORTED_BY_CALLBACK;goto done;}
#define OPTION(option,value) do {code=curl_easy_setopt(curl,option,value);if(code!=CURLE_OK)goto done;} while(0)
    OPTION(CURLOPT_URL,slot->url);
    OPTION(CURLOPT_PROTOCOLS_STR,"http,https");
    OPTION(CURLOPT_FOLLOWLOCATION,0L); /* Foundation owns redirects. */
    OPTION(CURLOPT_NOSIGNAL,1L);
    OPTION(CURLOPT_TIMEOUT_MS,(long)((slot->deadline_ns-ns+999999)/1000000));
    OPTION(CURLOPT_HTTPHEADER,fields);
    OPTION(CURLOPT_WRITEFUNCTION,append_bytes);OPTION(CURLOPT_WRITEDATA,&body);
    OPTION(CURLOPT_HEADERFUNCTION,response_header);OPTION(CURLOPT_HEADERDATA,&headers);
    OPTION(CURLOPT_NOPROGRESS,0L);OPTION(CURLOPT_XFERINFOFUNCTION,cancelled);OPTION(CURLOPT_XFERINFODATA,slot);
    if(slot->request_size) {
        OPTION(CURLOPT_POSTFIELDS,slot->request_body);
        OPTION(CURLOPT_POSTFIELDSIZE_LARGE,(curl_off_t)slot->request_size);
    }
    OPTION(CURLOPT_CUSTOMREQUEST,slot->method);
    if(!strcmp(slot->method,"HEAD"))OPTION(CURLOPT_NOBODY,1L);
    code=curl_easy_perform(curl);
#undef OPTION
done:
    long status=0;
    if(curl)curl_easy_getinfo(curl,CURLINFO_RESPONSE_CODE,&status);
    slot->response=(struct arlinux_http_response){.body=body.bytes,.headers=headers.bytes,
        .body_size=body.size,.header_size=headers.size,.status=(uint32_t)status,
        .error=body.overflow || headers.overflow ? AR_HTTP_TOO_LARGE : transport_error(code),.transport_code=code};
    if(curl)curl_easy_cleanup(curl);
    curl_slist_free_all(fields);
    if(slot->request_body)munmap(slot->request_body,slot->request_size);
    slot->request_body=NULL;
    /* Status/length/error only: do not log URLs, headers or response bodies. */
    fprintf(stderr,"LINUX_HTTP_COMPLETE status=%ld bytes=%zu error=%u transport=%u\n",
        status,body.size,slot->response.error,(unsigned int)code);
    __atomic_store_n(&slot->state,HTTP_DONE,__ATOMIC_RELEASE);
}
static void *http_worker(void *unused)
{
    (void)unused;
    for(;;) {
        for(unsigned int i=0;i<ARLINUX_HTTP_SLOTS;i++) {
            struct native_http_slot *slot=native_http_slots+i;
            if(__atomic_load_n(&slot->state,__ATOMIC_ACQUIRE)==HTTP_PENDING) {
                __atomic_store_n(&slot->state,HTTP_WORKING,__ATOMIC_RELEASE);transfer(slot);
            }
            if(__atomic_load_n(&slot->state,__ATOMIC_ACQUIRE)==HTTP_DONE &&
               __atomic_load_n(&slot->released,__ATOMIC_ACQUIRE)) {
                free((void *)slot->response.body);free((void *)slot->response.headers);
                __atomic_store_n(&slot->state,HTTP_FREE,__ATOMIC_RELEASE);
            }
        }
        struct pollfd wake={.fd=native_http_event,.events=POLLIN};
        if(poll(&wake,1,100)>0){uint64_t value;(void)read(native_http_event,&value,sizeof(value));}
    }
    return NULL;
}
int native_http_start(void)
{
    if(curl_global_init(CURL_GLOBAL_DEFAULT)!=CURLE_OK)return EIO;
    native_http_event=eventfd(0,EFD_CLOEXEC|EFD_NONBLOCK);
    if(native_http_event<0)return errno;
    pthread_t worker;int error=pthread_create(&worker,NULL,http_worker,NULL);
    if(error){close(native_http_event);native_http_event=-1;return error;}
    return pthread_detach(worker);
}
