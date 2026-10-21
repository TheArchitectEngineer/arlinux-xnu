/* Opt-in, read-only lifecycle ABI evidence from the actual loaded firmware.
 * No app actions or lifecycle transitions are supplied by this observer.
 * Method names in historical header dumps are only discovery hints. */
#include <objc/runtime.h>
#import <Foundation/Foundation.h>
#import <UIKit/UIKit.h>
#include <CoreFoundation/CoreFoundation.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../ios-platform-abi.h"

static const struct arlinux_ios_platform_api *platform;
static uint32_t last_generation=UINT32_MAX;
static uint32_t last_flags=UINT32_MAX;
static unsigned int samples;
static void (*original_control_action)(id,SEL,SEL,id,id);
static unsigned int control_actions;
static void observe_control_action(id control,SEL method,SEL action,id target,id event)
{
    unsigned int sequence=__atomic_fetch_add(&control_actions,1,__ATOMIC_RELAXED);
    if(sequence<64)
        fprintf(stderr,"IOS_CONTROL_ACTION sequence=%u stage=enter control=%.96s action=%.160s target=%.96s event=%p\n",
            sequence,class_getName(object_getClass(control)),action ? sel_getName(action) : "<none>",
            target ? class_getName(object_getClass(target)) : "<responder-chain>",event);
    else if(sequence==64)fprintf(stderr,"IOS_CONTROL_ACTION_TRUNCATED limit=64\n");
    original_control_action(control,method,action,target,event);
    if(sequence<64)fprintf(stderr,"IOS_CONTROL_ACTION sequence=%u stage=return\n",sequence);
}
struct protocol_evidence {
    Protocol *seen[32];
    unsigned int protocols,methods,missing,encoding_differences;
    int truncated;
};
static void describe_protocol(Protocol *protocol,Class adapter,struct protocol_evidence *evidence)
{
    for (unsigned int i=0;i<evidence->protocols;i++)
        if (evidence->seen[i]==protocol) return;
    if (evidence->protocols==32) { evidence->truncated=1; return; }
    evidence->seen[evidence->protocols++]=protocol;
    for (unsigned int required=0;required<2;required++)
        for (unsigned int instance=0;instance<2;instance++) {
            unsigned int count=0;
            struct objc_method_description *methods=protocol_copyMethodDescriptionList(protocol,required,instance,&count);
            for (unsigned int i=0;i<count;i++) {
                if (evidence->methods==256) { evidence->truncated=1; break; }
                Method actual=adapter ? class_getInstanceMethod(instance ? adapter : object_getClass(adapter),methods[i].name) : NULL;
                const char *types=actual ? method_getTypeEncoding(actual) : NULL;
                const char *declared=methods[i].types;
                const char *status=!actual ? "missing" : !types || !declared ? "encoding-unavailable" :
                    strcmp(types,declared) ? "encoding-diff" : "encoding-equal";
                evidence->methods++;
                evidence->missing+=!actual;
                evidence->encoding_differences+=types && declared && strcmp(types,declared)!=0;
                fprintf(stderr,"IOS_LIFECYCLE_PROTOCOL protocol=%.96s required=%u kind=%c selector=%.160s types=%.256s adapter_types=%.256s status=%s\n",
                    protocol_getName(protocol),required,instance ? '-' : '+',sel_getName(methods[i].name),
                    declared ? declared : "<absent>",types ? types : "<absent>",status);
            }
            free(methods);
        }
    unsigned int count=0;
    Protocol **parents=protocol_copyProtocolList(protocol,&count);
    for (unsigned int i=0;i<count;i++) describe_protocol(parents[i],adapter,evidence);
    free(parents);
}
static void window_sample(CFRunLoopTimerRef timer,void *context)
{
    (void)context;
    struct arlinux_ios_window_state window;
    long error=platform->read_window(&window,sizeof(window));
    if (!error && (window.generation!=last_generation || window.flags!=last_flags)) {
        last_generation=window.generation;
        last_flags=window.flags;
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
        strstr(name,"Identity") || strstr(name,"identity") ||
        strstr(name,"Settings") || strstr(name,"settings") ||
        strstr(name,"Specification") || strstr(name,"specification") ||
        strstr(name,"Transition") || strstr(name,"transition") ||
        strstr(name,"Display") || strstr(name,"display") ||
        strstr(name,"Frame") || strstr(name,"frame") ||
        strstr(name,"Foreground") || strstr(name,"foreground") ||
        strstr(name,"Deactivation") || strstr(name,"deactivation") ||
        strstr(name,"Background") || strstr(name,"background") || strstr(name,"Diff") || strstr(name,"diff") ||
        strstr(name,"Orientation") || strstr(name,"orientation") ||
        strstr(name,"Connection") || strstr(name,"Focus") || strstr(name,"focus") ||
        strstr(name,"Color") || strstr(name,"color") || strstr(name,"Gamut") || strstr(name,"gamut") || strstr(name,"trait") ||
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
        fprintf(stderr,"IOS_LIFECYCLE_ABI class=%.96s kind=%c selector=%.160s types=%.256s implementation=%p\n",
            class_getName(cls),meta ? '+' : '-',name,types ? types : "<absent>",(void *)method_getImplementation(methods[i]));
        emitted++;
    }
    free(methods);
}
static NSUncaughtExceptionHandler *prior_uncaught;
static unsigned int recording_uncaught;
static void record_uncaught(NSException *exception)
{
    if(!__atomic_exchange_n(&recording_uncaught,1,__ATOMIC_RELAXED)) {
        fprintf(stderr,"IOS_UNCAUGHT_EXCEPTION name=%.256s reason=%.1024s\n",
            exception.name.UTF8String,exception.reason.UTF8String);
        NSArray *frames=exception.callStackReturnAddresses;
        for(NSUInteger i=0;i<frames.count && i<24;i++)
            fprintf(stderr,"IOS_UNCAUGHT_EXCEPTION_FRAME index=%lu pc=0x%llx\n",
                (unsigned long)i,[frames[i] unsignedLongLongValue]);
    }
    /* Keep the existing handler and the runtime's normal fatal outcome.
     * A returned diagnostic callback does not handle or suppress the throw. */
    if(prior_uncaught)prior_uncaught(exception);
}
static int pointer_evidence_enabled;
void arlinux_ios_pointer_diagnose(UITouch *touch,const char *stage)
{
    static unsigned int snapshots;
    if(!pointer_evidence_enabled || !touch || snapshots++>=8)return;
    if(snapshots==1) {
        Class classes[]={[UITouch class],objc_getClass("UITouchesEvent")};
        for(unsigned c=0;c<2;c++) {
            unsigned count=0,shown=0;Method *methods=class_copyMethodList(classes[c],&count);
            for(unsigned i=0;i<count;i++) {
                const char *name=sel_getName(method_getName(methods[i]));
                if(!strstr(name,"gesture") && !strstr(name,"Gesture") && !strstr(name,"_addTouch"))continue;
                if(shown++==32){fprintf(stderr,"IOS_POINTER_METHODS_TRUNCATED class=%s\n",class_getName(classes[c]));break;}
                fprintf(stderr,"IOS_POINTER_METHOD class=%s selector=%s types=%.256s\n",
                    class_getName(classes[c]),name,method_getTypeEncoding(methods[i]));
            }
            free(methods);
        }
    }
    UIView *view=touch.view;CGPoint point=view ? [touch locationInView:view] : CGPointZero;
    fprintf(stderr,"IOS_POINTER_STATE stage=%s phase=%ld view=%s local=%g,%g gestures=%lu\n",
        stage,(long)touch.phase,view ? class_getName(object_getClass(view)) : "<absent>",
        point.x,point.y,(unsigned long)touch.gestureRecognizers.count);
    unsigned depth=0,recognizers=0;
    for(UIView *node=view;node && depth<8;node=node.superview,depth++) {
        CGRect frame=node.frame,bounds=node.bounds;
        fprintf(stderr,"IOS_POINTER_ANCESTOR depth=%u class=%s frame=%g,%g,%g,%g bounds=%g,%g,%g,%g enabled=%d gestures=%lu\n",
            depth,class_getName(object_getClass(node)),frame.origin.x,frame.origin.y,frame.size.width,frame.size.height,
            bounds.origin.x,bounds.origin.y,bounds.size.width,bounds.size.height,node.userInteractionEnabled,
            (unsigned long)node.gestureRecognizers.count);
        for(UIGestureRecognizer *gesture in node.gestureRecognizers) {
            if(recognizers++>=24)break;
            fprintf(stderr,"IOS_POINTER_RECOGNIZER depth=%u class=%s enabled=%d state=%ld cancel=%d delay_begin=%d delay_end=%d\n",
                depth,class_getName(object_getClass(gesture)),gesture.enabled,(long)gesture.state,
                gesture.cancelsTouchesInView,gesture.delaysTouchesBegan,gesture.delaysTouchesEnded);
        }
    }
}
void arlinux_ios_lifecycle_diagnose(const struct arlinux_ios_platform_api *api)
{
    const char *enabled=getenv("ARLINUX_DIAGNOSE_LIFECYCLE");
    if (!enabled || strcmp(enabled,"1")) return;
    pointer_evidence_enabled=1;
    /* Observe only actual UIKit dispatch. Forward the exact original arguments;
     * never call the app's selector or supply a missing action/event. */
    Class control=objc_getClass("UIControl");
    Method action=control ? class_getInstanceMethod(control,sel_registerName("sendAction:to:forEvent:")) : NULL;
    const char *types=action ? method_getTypeEncoding(action) : NULL;
    if(types && !strcmp(types,"v40@0:8:16@24@32"))
        original_control_action=(void *)method_setImplementation(action,(IMP)observe_control_action);
    else fprintf(stderr,"IOS_CONTROL_ACTION_UNAVAILABLE types=%.256s\n",types ? types : "<absent>");
    prior_uncaught=NSGetUncaughtExceptionHandler();
    NSSetUncaughtExceptionHandler(record_uncaught);
    /* Candidate input/termination boundaries: inspect actual loaded signatures
     * before binding them. These getters do not create or deliver any event. */
    const struct {const char *cls,*selector;} input_methods[]={
        {"UITouch","setWindow:"},{"UITouch","setView:"},
        {"UITouch","setPhase:"},{"UITouch","setTapCount:"},
        {"UITouch","setTimestamp:"},{"UITouch","setGestureView:"},
        {"UITouch","_setLocationInWindow:resetPrevious:"},
        {"UITouch","_setIsFirstTouchForView:"},{"UITouch","_setHidEvent:"},
        {"UIEvent","_clearTouches"},{"UIEvent","_addTouch:forDelayedDelivery:"},
        {"UIEvent","_setHIDEvent:"},{"UIEvent","_setTimestamp:"},
        {"UITouchesEvent","_clearTouches"},{"UITouchesEvent","_addTouch:forDelayedDelivery:"},
        {"UIApplication","_touchesEvent"},{"UIApplication","sendEvent:"},
        {"UIApplication","_terminateWithStatus:"},{"UIApplication","_applicationWillTerminate"}
    };
    for(unsigned int i=0;i<sizeof(input_methods)/sizeof(*input_methods);i++) {
        Class cls=objc_getClass(input_methods[i].cls);
        Method method=cls ? class_getInstanceMethod(cls,sel_registerName(input_methods[i].selector)) : NULL;
        fprintf(stderr,"IOS_INPUT_ABI class=%s selector=%s types=%s implementation=%p\n",
            input_methods[i].cls,input_methods[i].selector,
            method ? method_getTypeEncoding(method) : "<absent>",
            method ? (void *)method_getImplementation(method) : NULL);
    }
    Protocol *protocol=objc_getProtocol("FBSSceneUpdater");
    Class adapter=objc_getClass("ARLinuxSceneUpdater");
    struct protocol_evidence evidence={0};
    if (protocol) describe_protocol(protocol,adapter,&evidence);
    fprintf(stderr,"IOS_LIFECYCLE_CONTRACT protocol=FBSSceneUpdater protocol_available=%d adapter_available=%d protocols=%u methods=%u missing=%u encoding_differences=%u truncated=%d app_acceptance=false\n",
        protocol!=NULL,adapter!=Nil,evidence.protocols,evidence.methods,evidence.missing,evidence.encoding_differences,evidence.truncated);
    const char *classes[]={"CUIDesignLibrary","FBSDisplayConfiguration","FBSMutableDisplayConfiguration","CADisplay",
        "UIDynamicCatalogColor","UIDynamicCatalogSystemColor","UIDynamicColor",
        "_UIRemoteKeyboards","FBSWorkspace","FBSScene","FBSBasicSceneAgent","FBSSceneParameters","FBSMutableSceneParameters",
        "FBSSceneIdentity","FBSSceneIdentityToken","FBSSceneDefinition","FBSMutableSceneDefinition",
        "FBSSceneSpecification","FBSSceneSettings","FBSMutableSceneSettings",
        "FBSSceneSettingsDiff","UIApplicationSceneSettingsDiff","BSSettingsDiff",
        "FBSSceneClientSettings","FBSMutableSceneClientSettings","FBSSceneTransitionContext",
        "UIApplicationSceneSpecification","UIApplicationSceneSettings",
        "UIMutableApplicationSceneSettings","UIApplicationSceneClientSettings",
        "UIMutableApplicationSceneClientSettings","UIApplicationSceneTransitionContext","UIApplication"};
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
