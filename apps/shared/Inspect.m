#import "Inspect.h"
#import <QuartzCore/QuartzCore.h>
#include <stdio.h>
#ifdef ARLINUX_PLATFORM_INSPECTION
#import <objc/runtime.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#endif
static NSMutableArray *elements;
static NSMutableSet *visited;
static UIWindow *current;
static __weak UIView *observedRoot;
static NSUInteger becameActive,resignedActive;
#ifdef ARLINUX_PLATFORM_INSPECTION
static NSArray *rect_values(CGRect rect) {
    return @[@(rect.origin.x),@(rect.origin.y),@(rect.size.width),@(rect.size.height)];
}
static void collect_geometry(UIView *view,NSMutableArray *records,unsigned depth) {
    if (depth>18 || records.count>=128 || view.hidden || view.alpha<0.01) return;
    CALayer *layer=view.layer;CGAffineTransform transform=view.transform;
    NSMutableArray *classes=[NSMutableArray new];
    for (Class cls=view.class;cls && classes.count<8;cls=class_getSuperclass(cls))
        [classes addObject:NSStringFromClass(cls)];
    [records addObject:@{@"depth":@(depth),@"classes":classes,@"frame":rect_values(view.frame),
        @"bounds":rect_values(view.bounds),@"transform":@[@(transform.a),@(transform.b),@(transform.c),@(transform.d),@(transform.tx),@(transform.ty)],
        @"layerFrame":rect_values(layer.frame),@"layerBounds":rect_values(layer.bounds),
        @"cornerRadius":@(layer.cornerRadius),@"masksToBounds":@(layer.masksToBounds),
        @"maskClass":layer.mask ? NSStringFromClass(layer.mask.class) : @"<absent>",
        @"maskBounds":rect_values(layer.mask ? layer.mask.bounds : CGRectZero),
        @"contentsScale":@(layer.contentsScale),@"contentsRect":rect_values(layer.contentsRect),
        @"contentsCenter":rect_values(layer.contentsCenter),@"contentsGravity":layer.contentsGravity ?: @"<absent>"}];
    for (UIView *child in view.subviews) collect_geometry(child,records,depth+1);
}
static void observe_geometry(UIWindow *window) {
    static BOOL recorded;
    const char *enabled=getenv("ARLINUX_DIAGNOSE_LIFECYCLE");
    if (recorded || !enabled || strcmp(enabled,"1")) return;
    recorded=YES;
    UIScreen *screen=window.screen;NSMutableArray *views=[NSMutableArray new];
    collect_geometry(window,views,0);
    NSDictionary *value=@{@"windowBounds":rect_values(window.bounds),
        @"screenBounds":rect_values(screen.bounds),@"nativeBounds":rect_values(screen.nativeBounds),
        @"screenScale":@(screen.scale),@"nativeScale":@(screen.nativeScale),
        @"idiom":@([UIDevice currentDevice].userInterfaceIdiom),
        @"horizontalSizeClass":@(window.traitCollection.horizontalSizeClass),
        @"verticalSizeClass":@(window.traitCollection.verticalSizeClass),
        @"views":views,@"viewLimit":@128,@"limitReached":@(views.count>=128)};
    NSData *json=[NSJSONSerialization dataWithJSONObject:value options:NSJSONWritingSortedKeys error:nil];
    if (json) { fwrite("APP_RENDER_GEOMETRY ",1,20,stdout);fwrite(json.bytes,1,json.length,stdout);fputc('\n',stdout);fflush(stdout); }
}
static void collect_controllers(UIViewController *controller,NSMutableArray *records,unsigned depth) {
    if(!controller || depth>8 || records.count>=16)return;
    NSMutableDictionary *record=[@{@"class":NSStringFromClass(controller.class),@"depth":@(depth),
        @"beingPresented":@(controller.isBeingPresented),@"beingDismissed":@(controller.isBeingDismissed),
        @"viewLoaded":@(controller.isViewLoaded),@"viewInWindow":@(controller.viewIfLoaded.window!=nil)} mutableCopy];
    id<UIViewControllerTransitionCoordinator> transition=controller.transitionCoordinator;
    if(transition) {
        double duration=transition.transitionDuration,percent=transition.percentComplete;
        record[@"transition"]=@{@"animated":@(transition.isAnimated),@"interactive":@(transition.isInteractive),
            @"cancelled":@(transition.isCancelled),@"initiallyInteractive":@(transition.initiallyInteractive),
            @"duration":isfinite(duration) ? @(duration) : [NSNull null],
            @"percentComplete":isfinite(percent) ? @(percent) : [NSNull null]};
    }
    [records addObject:record];
    for(UIViewController *child in controller.childViewControllers)collect_controllers(child,records,depth+1);
    collect_controllers(controller.presentedViewController,records,depth+1);
}
#endif
static void inspect(id object,unsigned depth) {
    if (!object || depth>18 || elements.count>300) return;
    NSValue *identity=[NSValue valueWithNonretainedObject:object];
    if ([visited containsObject:identity]) return; [visited addObject:identity];
    BOOL view=[object isKindOfClass:UIView.class];
    if (view && ([(UIView *)object isHidden] || [(UIView *)object alpha]<0.01)) return;
    NSString *text=nil; BOOL interactive=NO;
    if ([object isKindOfClass:UILabel.class]) text=[(UILabel *)object text];
    else if ([object isKindOfClass:UIButton.class]) { text=[(UIButton *)object titleForState:UIControlStateNormal]; interactive=YES; }
    else if ([object isKindOfClass:UITextField.class]) { UITextField *field=object; text=field.text.length ? field.text : field.placeholder; interactive=YES; }
    else if ([object isAccessibilityElement]) { text=[object accessibilityLabel]; interactive=([object accessibilityTraits]&UIAccessibilityTraitButton)!=0; }
    if (text.length) {
        CGRect frame=view ? [(UIView *)object convertRect:[(UIView *)object bounds] toView:current] : [current convertRect:[object accessibilityFrame] fromWindow:nil];
        NSMutableDictionary *entry=[@{@"text":text,@"interactive":@(interactive),@"rect":@[@(frame.origin.x),@(frame.origin.y),@(frame.size.width),@(frame.size.height)]} mutableCopy];
        if ([object isKindOfClass:UILabel.class]) {
            UILabel *label=object;
            UIColor *resolved=[label.textColor resolvedColorWithTraitCollection:label.traitCollection];
            CGFloat red=0,green=0,blue=0,alpha=0;
            BOOL hasRGBA=[resolved getRed:&red green:&green blue:&blue alpha:&alpha];
            entry[@"drawing"]=@{@"viewClass":NSStringFromClass(label.class),
                @"font":label.font.fontName ?: @"<absent>",@"textColor":label.textColor.description ?: @"<absent>",
                @"resolvedColor":resolved.description ?: @"<absent>",
                @"hasRGBA":@(hasRGBA),@"rgba":@[@(red),@(green),@(blue),@(alpha)],
                @"interfaceStyle":@(label.traitCollection.userInterfaceStyle),@"displayGamut":@(label.traitCollection.displayGamut),
                @"colorBundle":[NSBundle bundleForClass:UIColor.class].bundlePath ?: @"<absent>",
                @"tintColor":label.tintColor.description ?: @"<absent>",
                @"layerOpacity":@(label.layer.opacity),@"layerHidden":@(label.layer.hidden),
                @"contentsClass":label.layer.contents ? NSStringFromClass([label.layer.contents class]) : @"<absent>"};
        }
        [elements addObject:entry];
    }
    if (view) for (UIView *child in [(UIView *)object subviews]) inspect(child,depth+1);
    for (id child in [object accessibilityElements]) inspect(child,depth+1);
}
static void snapshot(void) {
    static NSData *previous;
    UIView *root=observedRoot;
#ifdef ARLINUX_PLATFORM_INSPECTION
    if (!root) {
        for (UIWindow *candidate in [UIApplication sharedApplication].windows)
            if (!candidate.hidden && candidate.alpha>0.01) { root=candidate; if (candidate.isKeyWindow) break; }
    }
#endif
    UIWindow *window=[root isKindOfClass:UIWindow.class] ? (UIWindow *)root : root.window;
    if (!window || window.hidden) return;
#ifdef ARLINUX_PLATFORM_INSPECTION
    observe_geometry(window);
#endif
    current=window;elements=[NSMutableArray new];visited=[NSMutableSet new];inspect(window,0);
    CGRect bounds=window.bounds;
    UIApplicationState state=[UIApplication sharedApplication].applicationState;
    NSString *stateName=state==UIApplicationStateActive ? @"active" :
        state==UIApplicationStateInactive ? @"inactive" : @"background";
    NSMutableDictionary *value=[@{@"viewport":@[@(bounds.size.width),@(bounds.size.height)],@"elements":elements,
        @"applicationState":stateName,@"activationNotifications":@{@"didBecomeActive":@(becameActive),@"willResignActive":@(resignedActive)}} mutableCopy];
#ifdef ARLINUX_PLATFORM_INSPECTION
    const char *diagnose=getenv("ARLINUX_DIAGNOSE_LIFECYCLE");
    if(diagnose && !strcmp(diagnose,"1")) {
        NSMutableArray *controllers=[NSMutableArray new];collect_controllers(window.rootViewController,controllers,0);
        value[@"controllers"]=controllers;value[@"controllerLimitReached"]=@(controllers.count>=16);
        value[@"animationsEnabled"]=@([UIView areAnimationsEnabled]);
    }
#endif
    NSData *json=[NSJSONSerialization dataWithJSONObject:value options:NSJSONWritingSortedKeys error:nil];
    if (json && ![json isEqual:previous]) {
        fwrite("APP_INSPECTION ",1,15,stdout);fwrite(json.bytes,1,json.length,stdout);fputc('\n',stdout);fflush(stdout);previous=json;
    }
    elements=nil;visited=nil;current=nil;
}
void AppInspectionStart(UIView *root) {
    observedRoot=root;
    static BOOL started; if (started) return;started=YES;
    /* Read actual UIKit notifications; never post notifications or invoke
     * application/delegate methods to make a journey's expected state. */
    [[NSNotificationCenter defaultCenter] addObserverForName:UIApplicationDidBecomeActiveNotification object:nil queue:nil
        usingBlock:^(NSNotification *note) { (void)note;becameActive++; }];
    [[NSNotificationCenter defaultCenter] addObserverForName:UIApplicationWillResignActiveNotification object:nil queue:nil
        usingBlock:^(NSNotification *note) { (void)note;resignedActive++; }];
    if (root) snapshot();
    NSTimer *timer=[NSTimer timerWithTimeInterval:0.2 repeats:YES block:^(NSTimer *timer) { (void)timer;snapshot(); }];
    [[NSRunLoop mainRunLoop] addTimer:timer forMode:NSRunLoopCommonModes];
}
#ifdef ARLINUX_PLATFORM_INSPECTION
/* Original binaries cannot call the fixture observer. Opt-in to the same
 * read-only public UIKit traversal on its actual main loop, without changing
 * their executable, delegate, controls or action dispatch. */
__attribute__((constructor)) static void inspect_original_application(void) {
    const char *enabled=getenv("ARLINUX_INSPECT_APPLICATION");
    if (enabled && !strcmp(enabled,"1")) AppInspectionStart(nil);
}
#endif
