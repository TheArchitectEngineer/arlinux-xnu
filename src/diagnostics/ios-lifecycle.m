/* Opt-in, read-only lifecycle ABI evidence from the actual loaded firmware.
 * No framework messages, method replacements or app callbacks are invoked.
 * Method names in historical header dumps are only discovery hints. */
#include <objc/runtime.h>
#include <CoreFoundation/CoreFoundation.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../ios-platform-abi.h"

static const struct arlinux_ios_platform_api *platform;
static uint32_t last_generation=UINT32_MAX;
static unsigned int samples;
static void window_sample(CFRunLoopTimerRef timer,void *context)
{
    (void)context;
    struct arlinux_ios_window_state window;
    long error=platform->read_window(&window,sizeof(window));
    if (!error && window.generation!=last_generation) {
        last_generation=window.generation;
        fprintf(stderr,"IOS_HOST_WINDOW generation=%u pid=%u incarnation=%u width=%u height=%u refresh_millihz=%u flags=%u\n",
            window.generation,window.pid,window.incarnation,window.width,window.height,
            window.refresh_millihz,window.flags);
    }
    if (++samples>=100 && timer) CFRunLoopTimerInvalidate(timer);
}
static int lifecycle_method(const char *name)
{
    return !strncmp(name,"init",4) || !strncmp(name,"_init",5) ||
        strstr(name,"delegate") || strstr(name,"Delegate") ||
        strstr(name,"Scene") || strstr(name,"scene") ||
        strstr(name,"workspace") || strstr(name,"Workspace") ||
        !strcmp(name,"_clientClass") || !strcmp(name,"parametersForSpecification:");
}
static void describe_methods(Class cls,int meta)
{
    unsigned int count=0,emitted=0;
    Method *methods=class_copyMethodList(meta ? object_getClass(cls) : cls,&count);
    for (unsigned int i=0;i<count && emitted<96;i++) {
        const char *name=sel_getName(method_getName(methods[i]));
        if (!name || !lifecycle_method(name)) continue;
        const char *types=method_getTypeEncoding(methods[i]);
        fprintf(stderr,"IOS_LIFECYCLE_ABI class=%.96s kind=%c selector=%.160s types=%.256s\n",
            class_getName(cls),meta ? '+' : '-',name,types ? types : "<absent>");
        emitted++;
    }
    free(methods);
}
void arlinux_ios_lifecycle_diagnose(const struct arlinux_ios_platform_api *api)
{
    const char *enabled=getenv("ARLINUX_DIAGNOSE_LIFECYCLE");
    if (!enabled || strcmp(enabled,"1")) return;
    const char *classes[]={"FBSWorkspace","FBSUIApplicationWorkspace","FBSWorkspaceClient",
        "FBSScene","FBSSceneImpl","FBSSceneParameters","FBSMutableSceneParameters",
        "UIApplicationSceneSpecification","UIApplicationSceneSettings",
        "UIMutableApplicationSceneSettings","UIApplicationSceneClientSettings","UIApplication"};
    for (unsigned int i=0;i<sizeof(classes)/sizeof(*classes);i++) {
        Class cls=objc_getClass(classes[i]);
        if (!cls) { fprintf(stderr,"IOS_LIFECYCLE_CLASS_UNAVAILABLE class=%s\n",classes[i]); continue; }
        describe_methods(cls,0); describe_methods(cls,1);
    }
    platform=api;
    window_sample(NULL,NULL);
    CFRunLoopTimerRef timer=CFRunLoopTimerCreate(NULL,CFAbsoluteTimeGetCurrent()+0.1,
        0.1,0,0,window_sample,NULL);
    if (timer) { CFRunLoopAddTimer(CFRunLoopGetMain(),timer,kCFRunLoopCommonModes); CFRelease(timer); }
}
