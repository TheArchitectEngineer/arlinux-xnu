/* Opt-in observation of genuine CoreUI calls. Forward exactly the original
 * arguments, return object and NSError pointer. No extra lookup, fallback,
 * replacement color or caller state is supplied by this tracer. */
#import <Foundation/Foundation.h>
#import <objc/runtime.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef id (*color_lookup)(id,SEL,long long,long long,long long,long long,long long,long long,long long,NSError **);
static color_lookup original_lookup;
typedef id (*error_factory)(id,SEL,id,NSInteger,id);
static error_factory original_error;
static error_factory original_error_init;
static unsigned int error_samples;
static int collecting_error;
static void observe_error(id domain,NSInteger code)
{
    if(collecting_error || error_samples>=8 ||
        !(([domain isEqual:NSPOSIXErrorDomain] && code==78) ||
          ([domain isEqual:NSCocoaErrorDomain] && code==256)))return;
    collecting_error=1;error_samples++;
    fprintf(stderr,"IOS_FILE_ERROR sample=%u domain=%s code=%ld\n",error_samples,
        [domain UTF8String],(long)code);
    NSArray *frames=[NSThread callStackReturnAddresses];
    for(NSUInteger i=0;i<frames.count && i<20;i++)
        fprintf(stderr,"IOS_FILE_ERROR_FRAME index=%lu pc=0x%llx\n",(unsigned long)i,
            [frames[i] unsignedLongLongValue]);
    collecting_error=0;
}
static id traced_error(id cls,SEL selector,id domain,NSInteger code,id info)
{
    id result=original_error(cls,selector,domain,code,info);
    observe_error(domain,code);
    return result;
}
static id traced_error_init(id instance,SEL selector,id domain,NSInteger code,id info)
{
    id result=original_error_init(instance,selector,domain,code,info);
    observe_error(domain,code);
    return result;
}
static id traced_lookup(id cls,SEL selector,long long name,long long design,long long palette,
    long long scheme,long long contrast,long long styling,long long gamut,NSError **error)
{
    id result=original_lookup(cls,selector,name,design,palette,scheme,contrast,styling,gamut,error);
    if (!result) {
        NSError *failure=error ? *error : nil;
        NSError *underlying=failure.userInfo[NSUnderlyingErrorKey];
        NSString *path=[failure.userInfo[NSFilePathErrorKey] description];
        if (![underlying isKindOfClass:[NSError class]]) underlying=nil;
        fprintf(stderr,"IOS_COLOR_LOOKUP_FAILED name=%lld design=%lld palette=%lld scheme=%lld contrast=%lld styling=%lld gamut=%lld domain=%.128s code=%ld underlying_domain=%.128s underlying_code=%ld path=%.512s description=%.1024s\n",
            name,design,palette,scheme,contrast,styling,gamut,
            failure ? failure.domain.UTF8String : "<absent>",(long)failure.code,
            underlying ? underlying.domain.UTF8String : "<absent>",(long)underlying.code,
            path ? path.UTF8String : "<absent>",
            failure ? failure.localizedDescription.UTF8String : "<absent>");
    }
    return result;
}
void arlinux_ios_color_trace_start(void)
{
    const char *enabled=getenv("ARLINUX_DIAGNOSE_LIFECYCLE");
    if (!enabled || strcmp(enabled,"1")) return;
    Method error_method=class_getClassMethod([NSError class],@selector(errorWithDomain:code:userInfo:));
    if(error_method && !strcmp(method_getTypeEncoding(error_method),"@40@0:8@16q24@32")) {
        original_error=(error_factory)method_getImplementation(error_method);
        method_setImplementation(error_method,(IMP)traced_error);
    } else fprintf(stderr,"IOS_FILE_ERROR_TRACE_UNAVAILABLE types=%s\n",
        error_method ? method_getTypeEncoding(error_method) : "<absent>");
    Method error_init=class_getInstanceMethod([NSError class],@selector(initWithDomain:code:userInfo:));
    if(error_init && !strcmp(method_getTypeEncoding(error_init),"@40@0:8@16q24@32")) {
        original_error_init=(error_factory)method_getImplementation(error_init);
        method_setImplementation(error_init,(IMP)traced_error_init);
    } else fprintf(stderr,"IOS_FILE_ERROR_INIT_TRACE_UNAVAILABLE types=%s\n",
        error_init ? method_getTypeEncoding(error_init) : "<absent>");
    Class cls=objc_getClass("CUIDesignLibrary");
    const char *name="colorWithName:designSystem:palette:colorScheme:contrast:styling:displayGamut:error:";
    Method method=cls ? class_getClassMethod(cls,sel_registerName(name)) : NULL;
    int supported=method && method_getNumberOfArguments(method)==10;
    char *type=method ? method_copyReturnType(method) : NULL;
    supported=supported && type && !strcmp(type,"@"); free(type);
    for (unsigned int i=2;supported && i<=9;i++) {
        type=method_copyArgumentType(method,i);
        supported=type && (i==9 ? !strcmp(type,"^@") : !strcmp(type,"q") || !strcmp(type,"Q"));
        free(type);
    }
    if (!supported) {
        fprintf(stderr,"IOS_COLOR_TRACE_UNAVAILABLE types=%s\n",method ? method_getTypeEncoding(method) : "<absent>");
        return;
    }
    original_lookup=(color_lookup)method_getImplementation(method);
    method_setImplementation(method,(IMP)traced_lookup);
}
