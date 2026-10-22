/* Foundation owns URLs and resource parsing; Linux owns file reachability.
 * NSURL's Apple volume/property path is not the authority for a Linux mount.
 * Actual stat follows symlinks and enforces host path-search permissions. This
 * is an advisory existence check, not a promise that a subsequent open succeeds.
 * Contract: developer.apple.com/documentation/foundation/nsurl/
 * checkresourceisreachableandreturnerror(_:)
 */
#import <Foundation/Foundation.h>
#import <objc/runtime.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "ios-platform-abi.h"

typedef BOOL (*reachability_method)(NSURL *,SEL,NSError **);
static reachability_method original_url,original_cfurl;
static unsigned int observed;
static const struct arlinux_ios_platform_api *storage;
typedef void (^file_access)(NSURL *);
typedef void (^file_pair_access)(NSURL *,NSURL *);
typedef void (*coordinate_one)(id,SEL,NSURL *,NSUInteger,NSError **,file_access);
typedef void (*coordinate_pair)(id,SEL,NSURL *,NSUInteger,NSURL *,NSUInteger,NSError **,file_pair_access);
static coordinate_one original_read,original_write;
static coordinate_pair original_read_write,original_write_write;
static unsigned int coordination_observations;
static void coordination_result(SEL selector,BOOL invoked,NSError **error)
{
    NSError *failure=!invoked && error ? *error : nil;
    NSError *underlying=failure.userInfo[NSUnderlyingErrorKey];
    if(![underlying isKindOfClass:[NSError class]])underlying=nil;
    fprintf(stderr,"IOS_FILE_COORDINATION selector=%s accessor=%d domain=%.128s code=%ld underlying=%.128s underlying_code=%ld description=%.1024s\n",
        sel_getName(selector),invoked,failure ? failure.domain.UTF8String : "<absent>",(long)failure.code,
        underlying ? underlying.domain.UTF8String : "<absent>",(long)underlying.code,
        failure ? failure.localizedDescription.UTF8String : "<absent>");
    if(!invoked) {
        NSArray *frames=[NSThread callStackReturnAddresses];
        for(NSUInteger index=0;index<MIN(frames.count,16);index++)
            fprintf(stderr,"IOS_FILE_COORDINATION_FRAME index=%lu pc=0x%llx\n",
                (unsigned long)index,[frames[index] unsignedLongLongValue]);
    }
}
static void trace_coordinate_one(id object,SEL selector,NSURL *url,NSUInteger options,NSError **error,file_access accessor,coordinate_one original)
{
    if(__atomic_fetch_add(&coordination_observations,1,__ATOMIC_RELAXED)>=16 || !accessor) {
        original(object,selector,url,options,error,accessor);return;
    }
    fprintf(stderr,"IOS_FILE_COORDINATION_BEGIN selector=%s options=%lu path=%.1024s\n",
        sel_getName(selector),(unsigned long)options,url.path.UTF8String ?: "<absent>");
    __block BOOL invoked=NO;
    original(object,selector,url,options,error,^(NSURL *actual){invoked=YES;accessor(actual);});
    coordination_result(selector,invoked,error);
}
static void trace_read(id o,SEL s,NSURL *u,NSUInteger f,NSError **e,file_access a)
{trace_coordinate_one(o,s,u,f,e,a,original_read);}
static void trace_write(id o,SEL s,NSURL *u,NSUInteger f,NSError **e,file_access a)
{trace_coordinate_one(o,s,u,f,e,a,original_write);}
static void trace_coordinate_pair(id object,SEL selector,NSURL *url,NSUInteger options,NSURL *second,NSUInteger second_options,NSError **error,file_pair_access accessor,coordinate_pair original)
{
    if(__atomic_fetch_add(&coordination_observations,1,__ATOMIC_RELAXED)>=16 || !accessor) {
        original(object,selector,url,options,second,second_options,error,accessor);return;
    }
    fprintf(stderr,"IOS_FILE_COORDINATION_BEGIN selector=%s options=%lu second_options=%lu path=%.1024s second_path=%.1024s\n",
        sel_getName(selector),(unsigned long)options,(unsigned long)second_options,
        url.path.UTF8String ?: "<absent>",second.path.UTF8String ?: "<absent>");
    __block BOOL invoked=NO;
    original(object,selector,url,options,second,second_options,error,^(NSURL *actual,NSURL *other){invoked=YES;accessor(actual,other);});
    coordination_result(selector,invoked,error);
}
static void trace_read_write(id o,SEL s,NSURL *u,NSUInteger f,NSURL *v,NSUInteger g,NSError **e,file_pair_access a)
{trace_coordinate_pair(o,s,u,f,v,g,e,a,original_read_write);}
static void trace_write_write(id o,SEL s,NSURL *u,NSUInteger f,NSURL *v,NSUInteger g,NSError **e,file_pair_access a)
{trace_coordinate_pair(o,s,u,f,v,g,e,a,original_write_write);}
static void coordination_trace_start(void)
{
    const char *enabled=getenv("ARLINUX_DIAGNOSE_LIFECYCLE");
    if(!enabled || strcmp(enabled,"1"))return;
    struct {const char *selector,*types;IMP replacement;void *original;} bindings[]={
        {"coordinateReadingItemAtURL:options:error:byAccessor:","v48@0:8@16Q24^@32@?40",(IMP)trace_read,&original_read},
        {"coordinateWritingItemAtURL:options:error:byAccessor:","v48@0:8@16Q24^@32@?40",(IMP)trace_write,&original_write},
        {"coordinateReadingItemAtURL:options:writingItemAtURL:options:error:byAccessor:","v64@0:8@16Q24@32Q40^@48@?56",(IMP)trace_read_write,&original_read_write},
        {"coordinateWritingItemAtURL:options:writingItemAtURL:options:error:byAccessor:","v64@0:8@16Q24@32Q40^@48@?56",(IMP)trace_write_write,&original_write_write}
    };
    for(unsigned int index=0;index<sizeof(bindings)/sizeof(*bindings);index++) {
        Method method=class_getInstanceMethod([NSFileCoordinator class],sel_registerName(bindings[index].selector));
        const char *encoding=method ? method_getTypeEncoding(method) : NULL;
        if(!encoding || strcmp(encoding,bindings[index].types)) {
            fprintf(stderr,"IOS_FILE_COORDINATION_TRACE_UNAVAILABLE selector=%s types=%s\n",bindings[index].selector,encoding ?: "<absent>");
            continue;
        }
        IMP original=method_setImplementation(method,bindings[index].replacement);
        memcpy(bindings[index].original,&original,sizeof(original));
    }
}
static NSURL *group_container(NSFileManager *manager,SEL selector,NSString *identifier)
{
    (void)manager;(void)selector;
    const char *name=identifier.UTF8String;
    if(!name)return nil;
    char path[4096];
    long result=storage->group_path(name,path,sizeof(path));
    fprintf(stderr,"IOS_GROUP_CONTAINER status=%ld identifier=%.255s\n",result,name);
    return result ? nil : [NSURL fileURLWithFileSystemRepresentation:path isDirectory:YES relativeToURL:nil];
}

static BOOL file_reachable(NSURL *url,SEL selector,NSError **error,reachability_method original)
{
    if(!url.isFileURL)return original(url,selector,error);
    const char *path=url.fileSystemRepresentation;
    struct stat metadata;
    int status=path ? stat(path,&metadata) : -1;
    int failure=path ? errno : EINVAL;
    if(__atomic_fetch_add(&observed,1,__ATOMIC_RELAXED)<16)fprintf(stderr,"IOS_FILE_REACHABILITY status=%d errno=%d path=%.1024s\n",
        status,status ? failure : 0,path ? path : "<unrepresentable>");
    if(!status)return YES;
    if(error) {
        NSInteger code=NSFileReadUnknownError;
        if(failure==ENOENT || failure==ENOTDIR)code=NSFileReadNoSuchFileError;
        else if(failure==EACCES || failure==EPERM)code=NSFileReadNoPermissionError;
        else if(failure==EINVAL || failure==ENAMETOOLONG)code=NSFileReadInvalidFileNameError;
        NSMutableDictionary *info=[NSMutableDictionary dictionaryWithObjectsAndKeys:
            url,NSURLErrorKey,[NSError errorWithDomain:NSPOSIXErrorDomain code:failure userInfo:nil],
            NSUnderlyingErrorKey,nil];
        if(url.path)info[NSFilePathErrorKey]=url.path;
        *error=[NSError errorWithDomain:NSCocoaErrorDomain code:code userInfo:info];
    }
    return NO;
}
static BOOL url_reachable(NSURL *url,SEL selector,NSError **error)
{return file_reachable(url,selector,error,original_url);}
static BOOL cfurl_reachable(NSURL *url,SEL selector,NSError **error)
{return file_reachable(url,selector,error,original_cfurl);}

void arlinux_ios_filesystem_start(const struct arlinux_ios_platform_api *api)
{
    storage=api;
    Method group=class_getInstanceMethod([NSFileManager class],@selector(containerURLForSecurityApplicationGroupIdentifier:));
    const char *group_encoding=group ? method_getTypeEncoding(group) : NULL;
    if(!api->group_path || !group_encoding || strcmp(group_encoding,"@24@0:8@16")) {
        fprintf(stderr,"IOS_GROUP_BINDING_FAILED encoding=%s\n",group_encoding ? group_encoding : "<absent>");
        abort();
    }
    method_setImplementation(group,(IMP)group_container);
    SEL selector=@selector(checkResourceIsReachableAndReturnError:);
    Method method=class_getInstanceMethod([NSURL class],selector);
    Class cfurl=objc_getClass("__NSCFURL");
    Method cfmethod=cfurl ? class_getInstanceMethod(cfurl,selector) : NULL;
    const char *encoding=method ? method_getTypeEncoding(method) : NULL;
    if(!encoding || strcmp(encoding,"B24@0:8^@16") ||
       (cfmethod && strcmp(method_getTypeEncoding(cfmethod),encoding))) {
        fprintf(stderr,"IOS_FILESYSTEM_BINDING_FAILED url=%s cfurl=%s\n",
            encoding ? encoding : "<absent>",cfmethod ? method_getTypeEncoding(cfmethod) : "<absent>");
        abort();
    }
    original_url=(reachability_method)method_getImplementation(method);
    /* Inherited methods are the same Method object. Replace each actual
     * implementation once, preserving the original for non-file URL schemes. */
    if(cfmethod && cfmethod!=method) {
        original_cfurl=(reachability_method)method_getImplementation(cfmethod);
        method_setImplementation(cfmethod,(IMP)cfurl_reachable);
    }
    method_setImplementation(method,(IMP)url_reachable);
    coordination_trace_start();
}
