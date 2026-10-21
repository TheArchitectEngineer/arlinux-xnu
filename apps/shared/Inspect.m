#import "Inspect.h"
#import <QuartzCore/QuartzCore.h>
#include <stdio.h>
#ifdef ARLINUX_PLATFORM_INSPECTION
#include <stdlib.h>
#include <string.h>
#endif
static NSMutableArray *elements;
static NSMutableSet *visited;
static UIWindow *current;
static __weak UIView *observedRoot;
static NSUInteger becameActive,resignedActive;
static void inspect(id object,unsigned depth) {
    if (!object || depth>18 || elements.count>300) return;
    NSValue *identity=[NSValue valueWithNonretainedObject:object];
    if ([visited containsObject:identity]) return; [visited addObject:identity];
    BOOL view=[object isKindOfClass:UIView.class];
    if (view && ([(UIView *)object isHidden] || [(UIView *)object alpha]<0.01)) return;
    NSString *text=nil; BOOL interactive=NO;
    if ([object isKindOfClass:UILabel.class]) {
        text=[(UILabel *)object text];
        /* Custom UIControls commonly render their title in a child UILabel.
         * Read its actual control ancestry; do not classify every label as a
         * passive result or duplicate a UIButton title already exported above. */
        UIView *ancestor=[(UIView *)object superview];
        while (ancestor && ![ancestor isKindOfClass:UIControl.class]) ancestor=ancestor.superview;
        if ([ancestor isKindOfClass:UIButton.class] && [(UIButton *)ancestor titleForState:UIControlStateNormal].length)
            text=nil;
        else if (ancestor) interactive=[(UIControl *)ancestor isEnabled] && ancestor.userInteractionEnabled;
    }
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
    current=window;elements=[NSMutableArray new];visited=[NSMutableSet new];inspect(window,0);
    CGRect bounds=window.bounds;
    UIApplicationState state=[UIApplication sharedApplication].applicationState;
    NSString *stateName=state==UIApplicationStateActive ? @"active" :
        state==UIApplicationStateInactive ? @"inactive" : @"background";
    NSDictionary *value=@{@"viewport":@[@(bounds.size.width),@(bounds.size.height)],@"elements":elements,
        @"applicationState":stateName,@"activationNotifications":@{@"didBecomeActive":@(becameActive),@"willResignActive":@(resignedActive)}};
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
