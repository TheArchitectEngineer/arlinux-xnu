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
}
