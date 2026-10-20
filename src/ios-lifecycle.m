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
- (id)mainConfiguration;
- (id)_workspace;
- (void)setIdentity:(id)identity;
- (void)setSpecification:(id)specification;
- (void)setDisplayConfiguration:(id)display;
- (void)setFrame:(CGRect)frame;
- (void)setForeground:(BOOL)foreground;
- (void)workspace:(id)workspace didCreateScene:(id)scene withTransitionContext:(id)context completion:(id)completion;
@end

static const struct arlinux_ios_platform_api *host;
static id owned_workspace,owned_scene;
static NSString *owned_identifier;
static int launch_phase;
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
    require_method([app class],"workspace:didCreateScene:withTransitionContext:completion:","v48@0:8@16@24@32@?40",NO);
    owned_workspace=[[app _workspace] retain];
    if (!owned_workspace || !definitionClass) unsupported("workspace/definition");
    owned_identifier=[[NSString alloc] initWithFormat:@"arlinux:%u:%u:main",window->pid,window->incarnation];
    NSString *workspaceName=@"io.arlinux.workspace";
    scene_stage="identity";
    id specification=[specificationClass specification];
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
    [settings setFrame:CGRectMake(0,0,window->width,window->height)];
    [settings setForeground:!!(window->flags&ARLINUX_IOS_WINDOW_ACTIVE)];
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
            scene_stage="UIKit scene delivery";
            fprintf(stderr,"IOS_SCENE_DELIVER_BEGIN identifier=%s\n",owned_identifier.UTF8String);
            [app workspace:owned_workspace didCreateScene:owned_scene withTransitionContext:context completion:^{
                fprintf(stderr,"IOS_SCENE_DELIVERY_COMPLETION\n");
            }];
            fprintf(stderr,"IOS_SCENE_DELIVER_RETURNED app_acceptance=false\n");
        } @catch (NSException *exception) { log_scene_exception(exception); @throw; }
    }];
}
static void lifecycle_tick(CFRunLoopTimerRef timer,void *info)
{
    (void)timer; (void)info;
    if (launch_phase) return;
    struct arlinux_ios_window_state window;
    if (host->read_window(&window,sizeof(window)) ||
        !(window.flags&ARLINUX_IOS_WINDOW_CONFIGURED) || !(window.flags&ARLINUX_IOS_WINDOW_BACKEND_ALIVE)) return;
    @autoreleasepool {
        UIApplication *app=[UIApplication sharedApplication];
        if (!app.delegate) return;
        launch_phase=1;
        @try { create_scene(app,&window); }
        @catch (NSException *exception) {
            log_scene_exception(exception);
            @throw;
        }
        /* Activation updates and normal close are not implemented yet. No
         * successful lifecycle state or app readiness is fabricated here. */
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
