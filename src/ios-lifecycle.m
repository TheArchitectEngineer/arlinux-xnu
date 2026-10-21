/* Linux window -> genuine framework scene. Runs only on the guest main loop.
 * The adapter supplies platform state; UIKit owns app callbacks and view trees.
 * Private selectors are the measured iOS 16 userland ABI, not address patches.
 */
#import <UIKit/UIKit.h>
#import <objc/runtime.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "ios-platform-abi.h"

@interface NSObject (ARLinuxSceneABI)
+ (id)specification;
+ (id)queueWithMainRunLoopModes:(id)modes;
- (void)performAsync:(void (^)(void))block;
+ (id)identityForIdentifier:(id)identifier workspaceIdentifier:(id)workspace;
- (id)initWithIdentifier:(id)identifier workspace:(id)workspace endpoint:(id)endpoint;
- (id)_initWithUpdater:(id)updater identityToken:(id)token identity:(id)identity
    specification:(id)specification settings:(id)settings initialClientSettings:(id)client;
- (Class)settingsClass;
- (Class)clientSettingsClass;
- (Class)transitionContextClass;
- (id)settings;
+ (id)diffFromSettings:(id)before toSettings:(id)after;
- (id)mainConfiguration;
- (id)_workspace;
- (void)setIdentity:(id)identity;
- (void)setSpecification:(id)specification;
- (void)setDisplayConfiguration:(id)display;
- (void)setFrame:(CGRect)frame;
- (void)setForeground:(BOOL)foreground;
- (void)setDeactivationReasons:(unsigned long long)reasons;
- (void)updater:(id)updater didUpdateSettings:(id)settings withDiff:(id)diff
    transitionContext:(id)context completion:(void (^)(id))completion;
- (void)_callOutQueue_agent_didCreateWithTransitionContext:(id)context completion:(void (^)(void))completion;
- (void)_terminateWithStatus:(int)status;
@end

static const struct arlinux_ios_platform_api *host;
static id owned_workspace,owned_scene;
static NSString *owned_identifier;
static int launch_phase;
static id scene_specification;
static int scene_created,scene_update_pending;
static struct arlinux_ios_window_state applied_window;
/* This backend owns its scene's platform settings. One opaque nonzero
 * deactivation contribution represents the compositor withholding input focus;
 * it does not model an Apple lock screen or claim a backgrounded process. */
enum { ARLinuxHostFocusDeactivation=1 };
static const char *scene_stage="waiting";
id arlinux_ios_scene_token(NSString *identifier,NSString *workspace);
static void log_scene_exception(NSException *exception)
{
    fprintf(stderr,"IOS_SCENE_EXCEPTION stage=%s name=%.256s reason=%.1024s\n",
        scene_stage,exception.name.UTF8String,exception.reason.UTF8String);
}
static id (*original_scenes)(id,SEL);
static id (*original_scene)(id,SEL,id);

static void unsupported(const char *operation)
{
    fprintf(stderr,"IOS_LIFECYCLE_UNSUPPORTED operation=%s\n",operation);
    abort();
}
static void require_method(Class cls,const char *name,const char *types,BOOL meta)
{
    Method method=cls ? class_getInstanceMethod(meta ? object_getClass(cls) : cls,sel_registerName(name)) : NULL;
    const char *actual=method ? method_getTypeEncoding(method) : NULL;
    if (!actual || strcmp(actual,types)) {
        fprintf(stderr,"IOS_LIFECYCLE_BINDING_FAILED class=%s selector=%s expected=%s actual=%s\n",
            cls ? class_getName(cls) : "<absent>",name,types,actual ? actual : "<absent>");
        abort();
    }
}

@interface ARLinuxSceneUpdater : NSObject
@property(nonatomic,retain) id definition;
@property(nonatomic,retain) id callOutQueue;
@property(nonatomic,retain) id reportedClientSettings;
@end
@implementation ARLinuxSceneUpdater
/* Permit the genuine FBSScene/UIKit agent to be instantiated. This is a
 * software lifecycle policy, not an agent-ready or launch-success result. */
- (BOOL)canHaveAgent { return YES; }
- (id)endpoint { return nil; } /* No remote scene endpoint is exported yet. */
- (id)hostProcess { unsupported("hostProcess"); return nil; }
- (void)scene:(id)scene didUpdateClientSettings:(id)settings withDiff:(id)diff transitionContext:(id)context
{
    (void)scene; (void)diff; (void)context;
    self.reportedClientSettings=[[settings copy] autorelease];
    fprintf(stderr,"IOS_SCENE_CLIENT_SETTINGS_RECORDED\n");
}
- (void)scene:(id)scene didReceiveActions:(NSSet *)actions
{ (void)scene; if (actions.count) unsupported("scene actions"); }
- (void)scene:(id)scene sendMessage:(id)message withResponse:(void (^)(id,NSError *))response
{
    (void)scene; (void)message;
    fprintf(stderr,"IOS_SCENE_MESSAGE_UNSUPPORTED\n");
    if (response) response(nil,[NSError errorWithDomain:NSPOSIXErrorDomain code:ENOTSUP userInfo:nil]);
}
@end
static ARLinuxSceneUpdater *updater;

static void configure_settings(id settings,const struct arlinux_ios_window_state *window)
{
    require_method([settings class],"setDeactivationReasons:","v24@0:8Q16",NO);
    CGFloat scale=UIScreen.mainScreen.scale;
    if (!isfinite(scale) || scale<=0) unsupported("screen point scale");
    /* Host surface geometry is in buffer pixels. FrontBoard scene frames are
     * UIKit points, matching the real UIScreen's scale and logical bounds. */
    [settings setFrame:CGRectMake(0,0,window->width/scale,window->height/scale)];
    /* A configured live surface remains in the foreground when another
     * window takes focus. Only the activation contribution changes. */
    [settings setForeground:YES];
    [settings setDeactivationReasons:window->flags&ARLINUX_IOS_WINDOW_ACTIVE ? 0 : ARLinuxHostFocusDeactivation];
}
static void update_scene(const struct arlinux_ios_window_state *window)
{
    if(!scene_created || scene_update_pending)return;
    if(window->pid!=applied_window.pid || window->incarnation!=applied_window.incarnation)
        unsupported("scene incarnation changed");
    if(window->width==applied_window.width && window->height==applied_window.height &&
        !((window->flags^applied_window.flags)&ARLINUX_IOS_WINDOW_ACTIVE))return;
    struct arlinux_ios_window_state next=*window;
    scene_update_pending=1;
    [updater.callOutQueue performAsync:^{
        @try {
            scene_stage="scene settings update";
            id before=[owned_scene settings];
            id mutable=[[before mutableCopy] autorelease];
            configure_settings(mutable,&next);
            id after=[[mutable copy] autorelease];
            Class diffClass=objc_getClass("FBSSceneSettingsDiff");
            require_method(diffClass,"diffFromSettings:toSettings:","@32@0:8@16@24",YES);
            id diff=[diffClass diffFromSettings:before toSettings:after];
            id context=[[[[scene_specification transitionContextClass] alloc] init] autorelease];
            if(!before || !after || !diff || !context)unsupported("scene update inputs");
            fprintf(stderr,"IOS_SCENE_UPDATE_BEGIN pid=%u incarnation=%u active=%u width=%u height=%u\n",
                next.pid,next.incarnation,!!(next.flags&ARLINUX_IOS_WINDOW_ACTIVE),next.width,next.height);
            [owned_scene updater:updater didUpdateSettings:after withDiff:diff transitionContext:context completion:^(id result) {
                /* The update entry does not declare an NSError-out contract.
                 * Retain its opaque completion value as evidence; delivery of
                 * a callback is not proof that UIKit reached the desired state. */
                applied_window=next;scene_update_pending=0;
                fprintf(stderr,"IOS_SCENE_UPDATE_COMPLETION pid=%u active=%u value=%p app_acceptance=false\n",
                    next.pid,!!(next.flags&ARLINUX_IOS_WINDOW_ACTIVE),(void *)result);
            }];
        } @catch(NSException *exception) {log_scene_exception(exception);@throw;}
    }];
}

static id workspace_scenes(id object,SEL selector)
{ return object==owned_workspace && owned_scene ? @[owned_scene] : original_scenes(object,selector); }
static id workspace_scene(id object,SEL selector,id identifier)
{
    if (object==owned_workspace && owned_scene && [owned_identifier isEqual:identifier]) return owned_scene;
    return original_scene(object,selector,identifier);
}
static void install_registry(void)
{
    Class cls=objc_getClass("FBSWorkspace");
    require_method(cls,"scenes","@16@0:8",NO);
    require_method(cls,"sceneWithIdentifier:","@24@0:8@16",NO);
    original_scenes=(void *)method_setImplementation(class_getInstanceMethod(cls,@selector(scenes)),(IMP)workspace_scenes);
    original_scene=(void *)method_setImplementation(class_getInstanceMethod(cls,@selector(sceneWithIdentifier:)),(IMP)workspace_scene);
}
static void create_scene(UIApplication *app,const struct arlinux_ios_window_state *window)
{
    Class sceneClass=objc_getClass("FBSScene"),identityClass=objc_getClass("FBSSceneIdentity");
    Class definitionClass=objc_getClass("FBSSceneDefinition");
    Class specificationClass=objc_getClass("UIApplicationSceneSpecification");
    Class queueClass=objc_getClass("FBSSerialQueue"),monitorClass=objc_getClass("FBSDisplayMonitor");
    require_method(sceneClass,"_initWithUpdater:identityToken:identity:specification:settings:initialClientSettings:","@64@0:8@16@24@32@40@48@56",NO);
    require_method(identityClass,"identityForIdentifier:workspaceIdentifier:","@32@0:8@16@24",YES);
    require_method(queueClass,"queueWithMainRunLoopModes:","@24@0:8@16",YES);
    require_method(queueClass,"performAsync:","v24@0:8@?16",NO);
    require_method(specificationClass,"specification","@16@0:8",YES);
    require_method(monitorClass,"mainConfiguration","@16@0:8",NO);
    require_method([app class],"_workspace","@16@0:8",NO);
    require_method(sceneClass,"_callOutQueue_agent_didCreateWithTransitionContext:completion:","v32@0:8@16@?24",NO);
    require_method(sceneClass,"settings","@16@0:8",NO);
    require_method(sceneClass,"updater:didUpdateSettings:withDiff:transitionContext:completion:","v56@0:8@16@24@32@40@?48",NO);
    owned_workspace=[[app _workspace] retain];
    if (!owned_workspace || !definitionClass) unsupported("workspace/definition");
    owned_identifier=[[NSString alloc] initWithFormat:@"arlinux:%u:%u:main",window->pid,window->incarnation];
    NSString *workspaceName=@"io.arlinux.workspace";
    scene_stage="identity";
    id specification=[specificationClass specification];
    scene_specification=[specification retain];
    id identity=[identityClass identityForIdentifier:owned_identifier workspaceIdentifier:workspaceName];
    id token=arlinux_ios_scene_token(owned_identifier,workspaceName);
    scene_stage="display configuration";
    id monitor=[[[monitorClass alloc] init] autorelease];
    id display=[monitor mainConfiguration];
    if (!identity || !token || !specification || !display) {
        fprintf(stderr,"IOS_SCENE_INPUTS specification=%d identity=%d token=%d display=%d\n",
            specification!=nil,identity!=nil,token!=nil,display!=nil);
        unsupported("scene inputs");
    }
    scene_stage="settings";
    id settings=[[[[specification settingsClass] alloc] init] autorelease];
    settings=[[settings mutableCopy] autorelease];
    require_method([settings class],"setFrame:","v48@0:8{CGRect={CGPoint=dd}{CGSize=dd}}16",NO);
    require_method([settings class],"setForeground:","v20@0:8B16",NO);
    require_method([settings class],"setDisplayConfiguration:","v24@0:8@16",NO);
    [settings setDisplayConfiguration:display];
    configure_settings(settings,window);
    /* FBSScene requires the specification's exact immutable class, not its
     * mutable configuration counterpart (original fault 0x19b93237c). */
    settings=[[settings copy] autorelease];
    id client=[[[[specification clientSettingsClass] alloc] init] autorelease];
    updater=[[ARLinuxSceneUpdater alloc] init];
    updater.definition=[[[definitionClass alloc] init] autorelease];
    [updater.definition setIdentity:identity]; [updater.definition setSpecification:specification];
    updater.callOutQueue=[queueClass queueWithMainRunLoopModes:@[(id)kCFRunLoopCommonModes]];
    struct arlinux_ios_window_state initial=*window;
    scene_stage="queued scene initialization";
    [updater.callOutQueue performAsync:^{
        @try {
            scene_stage="scene initialization";
            fprintf(stderr,"IOS_SCENE_CONSTRUCT_BEGIN pid=%u incarnation=%u width=%u height=%u settings=%s\n",
                initial.pid,initial.incarnation,initial.width,initial.height,class_getName([settings class]));
            owned_scene=[[sceneClass alloc] _initWithUpdater:updater identityToken:token identity:identity
                specification:specification settings:settings initialClientSettings:client];
            if (!owned_scene) unsupported("scene initializer");
            install_registry();
            id context=[[[[specification transitionContextClass] alloc] init] autorelease];
            if (!context) unsupported("transition context");
            /* The genuine scene creation notification binds its agent before
             * UIApplicationSceneClientAgent dispatches into UIKit. Calling
             * UIApplication's workspace callback directly skipped that binding
             * and trapped later at FBSBasicSceneAgent's _scene == scene check.
             * Keep the original agent event/completion path intact. */
            scene_stage="scene-agent creation delivery";
            fprintf(stderr,"IOS_SCENE_DELIVER_BEGIN identifier=%s\n",owned_identifier.UTF8String);
            [owned_scene _callOutQueue_agent_didCreateWithTransitionContext:context completion:^{
                applied_window=initial;scene_created=1;
                fprintf(stderr,"IOS_SCENE_DELIVERY_COMPLETION\n");
            }];
            fprintf(stderr,"IOS_SCENE_DELIVER_RETURNED app_acceptance=false\n");
        } @catch (NSException *exception) { log_scene_exception(exception); @throw; }
    }];
}
static void lifecycle_tick(CFRunLoopTimerRef timer,void *info)
{
    (void)timer; (void)info;
    struct arlinux_ios_window_state window;
    if(host->read_window(&window,sizeof(window)))return;
    if(launch_phase) {
        if(launch_phase==1 && (window.flags&ARLINUX_IOS_WINDOW_CLOSE_REQUESTED)) {
            launch_phase=2;
            UIApplication *app=[UIApplication sharedApplication];
            require_method([app class],"_terminateWithStatus:","v20@0:8i16",NO);
            fprintf(stderr,"IOS_LIFECYCLE_CLOSE pid=%u incarnation=%u source=host-window\n",window.pid,window.incarnation);
            [app _terminateWithStatus:0];
        }
        if(launch_phase==1 && (window.flags&ARLINUX_IOS_WINDOW_CONFIGURED) &&
            (window.flags&ARLINUX_IOS_WINDOW_BACKEND_ALIVE))update_scene(&window);
        return;
    }
    if(!(window.flags&ARLINUX_IOS_WINDOW_CONFIGURED) || !(window.flags&ARLINUX_IOS_WINDOW_BACKEND_ALIVE))return;
    @autoreleasepool {
        UIApplication *app=[UIApplication sharedApplication];
        if (!app.delegate) return;
        launch_phase=1;
        @try { create_scene(app,&window); }
        @catch (NSException *exception) {
            log_scene_exception(exception);
            @throw;
        }
    }
}
void arlinux_ios_lifecycle_start(const struct arlinux_ios_platform_api *api)
{
    host=api;
    CFRunLoopTimerRef timer=CFRunLoopTimerCreate(NULL,CFAbsoluteTimeGetCurrent()+0.05,
        0.05,0,0,lifecycle_tick,NULL);
    if (!timer) unsupported("main-loop timer");
    CFRunLoopAddTimer(CFRunLoopGetMain(),timer,kCFRunLoopCommonModes); CFRelease(timer);
}
