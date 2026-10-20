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
static id traced_lookup(id cls,SEL selector,long long name,long long design,long long palette,
    long long scheme,long long contrast,long long styling,long long gamut,NSError **error)
{
    id result=original_lookup(cls,selector,name,design,palette,scheme,contrast,styling,gamut,error);
    if (!result) {
        NSError *failure=error ? *error : nil;
        fprintf(stderr,"IOS_COLOR_LOOKUP_FAILED name=%lld design=%lld palette=%lld scheme=%lld contrast=%lld styling=%lld gamut=%lld domain=%.128s code=%ld description=%.1024s\n",
            name,design,palette,scheme,contrast,styling,gamut,
            failure ? failure.domain.UTF8String : "<absent>",(long)failure.code,
            failure ? failure.localizedDescription.UTF8String : "<absent>");
    }
    return result;
}
void arlinux_ios_color_trace_start(void)
{
    const char *enabled=getenv("ARLINUX_DIAGNOSE_LIFECYCLE");
    if (!enabled || strcmp(enabled,"1")) return;
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
