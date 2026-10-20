/* Software-window platform policy. Preserve Apple's CAContext/CALayer object
 * semantics, but use its in-process context constructor instead of connecting
 * UIWindow to an absent Apple render server. Actual layer pixels are rendered
 * by the existing CG/Wayland backend; context creation is not presentation.
 * Remote context sharing and hardware compositor services are not supplied.
 */
#import <Foundation/Foundation.h>
#import <objc/runtime.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static id (*local_context)(id,SEL,id);
static SEL local_selector;
static id software_context(id cls,SEL selector,id options)
{
    (void)selector;
    id context=local_context(cls,local_selector,options);
    fprintf(stderr,"IOS_RENDER_LOCAL_CONTEXT class=%s created=%d app_acceptance=false\n",
        context ? class_getName(object_getClass(context)) : "<absent>",context!=nil);
    if (!context) abort();
    return context;
}
void arlinux_ios_rendering_start(void)
{
    Class cls=objc_getClass("CAContext");
    Method remote=cls ? class_getClassMethod(cls,sel_registerName("remoteContextWithOptions:")) : NULL;
    local_selector=sel_registerName("localContextWithOptions:");
    Method local=cls ? class_getClassMethod(cls,local_selector) : NULL;
    const char *remote_types=remote ? method_getTypeEncoding(remote) : NULL;
    const char *local_types=local ? method_getTypeEncoding(local) : NULL;
    if (!remote_types || !local_types || strcmp(remote_types,"@24@0:8@16") || strcmp(local_types,"@24@0:8@16")) {
        fprintf(stderr,"IOS_RENDER_BINDING_FAILED remote=%s local=%s\n",
            remote_types ? remote_types : "<absent>",local_types ? local_types : "<absent>");
        abort();
    }
    local_context=(void *)method_getImplementation(local);
    method_setImplementation(remote,(IMP)software_context);
}
