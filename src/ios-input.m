/* Linux input -> original UIKit hit testing/event dispatch. The platform owns
 * contacts; UIKit owns controls/actions. No test harness calls into this path.
 * Private event construction follows the KIF community's measured selectors
 * (https://github.com/kif-framework/KIF);
 * no fake GSEvent layout, accessibility switch or control callback is used.
 */
#import <UIKit/UIKit.h>
#import <objc/runtime.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <dlfcn.h>
#include <mach/mach_time.h>
#include "ios-platform-abi.h"

typedef struct __IOHIDEvent *ARHIDEvent;
typedef ARHIDEvent (*ARHandCreate)(CFAllocatorRef,uint64_t,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,
    double,double,double,double,double,Boolean,Boolean,uint32_t);
typedef ARHIDEvent (*ARFingerCreate)(CFAllocatorRef,uint64_t,uint32_t,uint32_t,uint32_t,
    double,double,double,double,double,Boolean,Boolean,uint32_t);
static ARHandCreate create_hand;
static ARFingerCreate create_finger;
static void (*hid_integer)(ARHIDEvent,uint32_t,CFIndex);
static void (*hid_append)(ARHIDEvent,ARHIDEvent,uint32_t);
enum { AR_HID_HAND=3, AR_HID_RANGE=1, AR_HID_TOUCH=2, AR_HID_POSITION=4,
       AR_HID_CANCEL=0x80, AR_HID_DISPLAY_INTEGRATED=(11U<<16)+25 };

@interface UITouch (ARLinuxPointer)
- (void)setWindow:(UIWindow *)window;
- (void)setView:(UIView *)view;
- (void)setGestureView:(UIView *)view;
- (void)setTapCount:(NSUInteger)count;
- (void)setTimestamp:(NSTimeInterval)timestamp;
- (void)setPhase:(UITouchPhase)phase;
- (void)_setLocationInWindow:(CGPoint)point resetPrevious:(BOOL)reset;
- (void)_setIsFirstTouchForView:(BOOL)first;
- (void)_setHidEvent:(ARHIDEvent)event;
@end
@interface UIEvent (ARLinuxPointer)
- (void)_clearTouches;
- (void)_addTouch:(UITouch *)touch forDelayedDelivery:(BOOL)delayed;
- (void)_setHIDEvent:(ARHIDEvent)event;
- (void)_setTimestamp:(NSTimeInterval)timestamp;
@end
@interface UIApplication (ARLinuxPointer)
- (UIEvent *)_touchesEvent;
@end
static const struct arlinux_ios_platform_api *input_host;
static UITouch *contact;
static CGPoint contact_point;
static unsigned int pointer_bindings;
static void (*set_keyboard_attached)(Boolean,uint32_t);
static BOOL keyboard_available;
static int keyboard_state_known;
static BOOL host_keyboard_mode(id object,SEL selector)
{(void)object;(void)selector;return keyboard_available;}
static void update_keyboard_device(const struct arlinux_ios_window_state *window)
{
    BOOL available=(window->flags&(ARLINUX_IOS_WINDOW_BACKEND_ALIVE|ARLINUX_IOS_WINDOW_KEYBOARD_AVAILABLE))==
        (ARLINUX_IOS_WINDOW_BACKEND_ALIVE|ARLINUX_IOS_WINDOW_KEYBOARD_AVAILABLE);
    if(keyboard_state_known && keyboard_available==available)return;
    keyboard_state_known=1;keyboard_available=available;
    /* GraphicsServices and UIKit must agree on the actual host source. See
     * WebKit's UIScriptControllerIOS::setHardwareKeyboardAttached; this bridge
     * takes its state from the real Wayland seat, never a test setting. */
    set_keyboard_attached(available,0);
    fprintf(stderr,"IOS_HOST_KEYBOARD available=%d\n",available);
}
static id (*original_image_cache_init)(id,SEL,BOOL);
static id image_cache_without_apple_disk(id object,SEL selector,BOOL local)
{
    (void)local;
    /* The measured original initializer always constructs its NSCache. Only
     * local-access mode enters CheckSandboxAccess for Apple's shared cache
     * container. This backend supplies no such container: preserve the real
     * non-local initializer instead of fabricating its path or a cache hit.
     * Later original cache operations/rendering still determine actual output. */
    fprintf(stderr,"IOS_INPUT_IMAGE_CACHE apple_local_disk=false\n");
    return original_image_cache_init(object,selector,NO);
}

static void require_input_method(Class cls,const char *selector,const char *types)
{
    Method method=cls ? class_getInstanceMethod(cls,sel_registerName(selector)) : NULL;
    const char *actual=method ? method_getTypeEncoding(method) : NULL;
    if(!actual || strcmp(actual,types)) {
        fprintf(stderr,"IOS_INPUT_BINDING_FAILED class=%s selector=%s actual=%s\n",
            cls ? class_getName(cls) : "<absent>",selector,actual ? actual : "<absent>");
        abort();
    }
}
static void bind_pointer(void)
{
    if(pointer_bindings)return;
    const struct {Class cls;const char *selector,*types;} methods[]={
        {[UITouch class],"setWindow:","v24@0:8@16"},
        {[UITouch class],"setView:","v24@0:8@16"},
        {[UITouch class],"setGestureView:","v24@0:8@16"},
        {[UITouch class],"setTapCount:","v24@0:8Q16"},
        {[UITouch class],"setTimestamp:","v24@0:8d16"},
        {[UITouch class],"setPhase:","v24@0:8q16"},
        {[UITouch class],"_setLocationInWindow:resetPrevious:","v36@0:8{CGPoint=dd}16B32"},
        {[UITouch class],"_setIsFirstTouchForView:","v20@0:8B16"},
        {[UITouch class],"_setHidEvent:","v24@0:8^{__IOHIDEvent=}16"},
        {objc_getClass("UITouchesEvent"),"_clearTouches","v16@0:8"},
        {objc_getClass("UITouchesEvent"),"_addTouch:forDelayedDelivery:","v28@0:8@16B24"},
        {[UIEvent class],"_setHIDEvent:","v24@0:8^{__IOHIDEvent=}16"},
        {[UIEvent class],"_setTimestamp:","v24@0:8d16"},
        {[UIApplication class],"_touchesEvent","@16@0:8"}
    };
    for(unsigned int i=0;i<sizeof(methods)/sizeof(*methods);i++)
        require_input_method(methods[i].cls,methods[i].selector,methods[i].types);
    void *iokit=dlopen("/System/Library/Frameworks/IOKit.framework/IOKit",RTLD_NOW|RTLD_LOCAL);
    create_hand=iokit ? (ARHandCreate)dlsym(iokit,"IOHIDEventCreateDigitizerEvent") : NULL;
    create_finger=iokit ? (ARFingerCreate)dlsym(iokit,"IOHIDEventCreateDigitizerFingerEvent") : NULL;
    hid_integer=iokit ? (void *)dlsym(iokit,"IOHIDEventSetIntegerValue") : NULL;
    hid_append=iokit ? (void *)dlsym(iokit,"IOHIDEventAppendEvent") : NULL;
    if(!create_hand || !create_finger || !hid_integer || !hid_append) {
        fprintf(stderr,"IOS_INPUT_BINDING_FAILED operation=IOHIDEvent error=%.512s\n",dlerror());abort();
    }
    pointer_bindings=1;
}
static void deliver_contact(UITouchPhase phase,uint64_t time)
{
    if(!contact)return;
    BOOL touching=phase!=UITouchPhaseEnded && phase!=UITouchPhaseCancelled;
    uint32_t mask=phase==UITouchPhaseMoved ? AR_HID_POSITION : AR_HID_RANGE|AR_HID_TOUCH;
    if(phase==UITouchPhaseCancelled)mask|=AR_HID_CANCEL;
    ARHIDEvent hand=create_hand(NULL,time,AR_HID_HAND,0,0,mask,0,0,0,0,0,0,NO,touching,0);
    ARHIDEvent finger=create_finger(NULL,time,1,1,mask,contact_point.x,contact_point.y,0,0,0,touching,touching,0);
    if(!hand || !finger) {fprintf(stderr,"IOS_INPUT_EVENT_ALLOCATION_FAILED\n");abort();}
    hid_integer(hand,AR_HID_DISPLAY_INTEGRATED,1);
    hid_integer(finger,AR_HID_DISPLAY_INTEGRATED,1);
    hid_append(hand,finger,0);CFRelease(finger);
    [contact setPhase:phase];[contact setTimestamp:(double)time/1e9];
    [contact _setLocationInWindow:contact_point resetPrevious:phase==UITouchPhaseBegan];
    [contact _setHidEvent:hand];
    /* UIKit keeps the gesture routing view separately from the responder view.
     * Follow KIF's touch construction before the original event collects its
     * recognizers; no recognizer or application action is invoked directly. */
    if(phase==UITouchPhaseBegan)[contact setGestureView:contact.view];
    UIApplication *app=[UIApplication sharedApplication];
    UIEvent *event=[app _touchesEvent];
    if(!event) {fprintf(stderr,"IOS_INPUT_EVENT_UNAVAILABLE\n");abort();}
    [event _clearTouches];[event _setHIDEvent:hand];
    [event _setTimestamp:(double)time/1e9];
    [event _addTouch:contact forDelayedDelivery:NO];
    fprintf(stderr,"IOS_POINTER_DELIVER phase=%ld x=%g y=%g view=%s gestures=%lu\n",(long)phase,
        contact_point.x,contact_point.y,class_getName(object_getClass(contact.view)),
        (unsigned long)contact.gestureRecognizers.count);
    [app sendEvent:event];CFRelease(hand);
    if(!touching) {[event _clearTouches];[contact release];contact=nil;}
}
static id<UIKeyInput> find_text_target(UIView *view,unsigned int depth)
{
    if(!view || depth>64)return nil;
    if(view.isFirstResponder && [view conformsToProtocol:@protocol(UIKeyInput)])return (id<UIKeyInput>)view;
    for(UIView *child in view.subviews) {
        id<UIKeyInput> target=find_text_target(child,depth+1);
        if(target)return target;
    }
    return nil;
}
static void deliver_text(const struct arlinux_ios_input_event *event)
{
    UIApplication *app=[UIApplication sharedApplication];
    id<UIKeyInput> target=find_text_target(app.keyWindow,0);
    if(!target)for(UIWindow *window in app.windows) {
        target=find_text_target(window,0);if(target)break;
    }
    if(!target) {fprintf(stderr,"IOS_TEXT_INPUT_NO_RESPONDER kind=%u\n",event->kind);return;}
    /* UIKeyInput is the framework's text-commit boundary. Original editing,
     * delegate callbacks, selection and drawing stay in UIKit. Never search
     * for an app selector, alter a control value, or call its target/action. */
    fprintf(stderr,"IOS_TEXT_INPUT kind=%u responder=%s\n",event->kind,class_getName(object_getClass(target)));
    if(event->kind==ARLINUX_IOS_INPUT_DELETE) {[target deleteBackward];return;}
    size_t size=strnlen(event->data.utf8,sizeof(event->data.utf8));
    NSString *text=size<sizeof(event->data.utf8) ?
        [[[NSString alloc] initWithBytes:event->data.utf8 length:size encoding:NSUTF8StringEncoding] autorelease] : nil;
    if(!text) {fprintf(stderr,"IOS_TEXT_INPUT_INVALID_UTF8\n");abort();}
    [target insertText:text];
}
static void input_tick(CFRunLoopTimerRef timer,void *context)
{
    (void)timer;(void)context;
    @autoreleasepool {
        struct arlinux_ios_window_state window;
        if(input_host->read_window(&window,sizeof(window)))return;
        update_keyboard_device(&window);
        BOOL active=(window.flags&(ARLINUX_IOS_WINDOW_ACTIVE|ARLINUX_IOS_WINDOW_BACKEND_ALIVE))==
            (ARLINUX_IOS_WINDOW_ACTIVE|ARLINUX_IOS_WINDOW_BACKEND_ALIVE);
        if(!active && contact)deliver_contact(UITouchPhaseCancelled,mach_absolute_time());
        for(unsigned int i=0;i<ARLINUX_IOS_INPUT_CAPACITY;i++) {
            struct arlinux_ios_input_event pointer;
            long status=input_host->read_input(&pointer,sizeof(pointer));
            if(!status)break;
            if(status==-EOVERFLOW) {deliver_contact(UITouchPhaseCancelled,mach_absolute_time());break;}
            if(status<0) {fprintf(stderr,"IOS_INPUT_READ_FAILED status=%ld\n",status);abort();}
            if(!active)continue;
            if(pointer.kind==ARLINUX_IOS_INPUT_TEXT || pointer.kind==ARLINUX_IOS_INPUT_DELETE) {
                deliver_text(&pointer);continue;
            }
            if(pointer.kind!=ARLINUX_IOS_INPUT_POINTER) {
                fprintf(stderr,"IOS_INPUT_INVALID_KIND kind=%u\n",pointer.kind);abort();
            }
            bind_pointer();
            if(pointer.data.pointer.phase==ARLINUX_IOS_POINTER_DOWN) {
                if(contact)deliver_contact(UITouchPhaseCancelled,pointer.nanoseconds);
                UIWindow *window=[UIApplication sharedApplication].keyWindow;
                CGPoint point=[window convertPoint:CGPointMake(pointer.data.pointer.x_fixed/256.0,pointer.data.pointer.y_fixed/256.0) fromWindow:nil];
                UIView *view=[window hitTest:point withEvent:nil];
                if(!window || !view)continue;
                contact=[[UITouch alloc] init];[contact setWindow:window];[contact setTapCount:1];
                contact_point=point;[contact _setLocationInWindow:point resetPrevious:YES];
                [contact setView:view];[contact _setIsFirstTouchForView:YES];
                deliver_contact(UITouchPhaseBegan,pointer.nanoseconds);
            } else if(contact) {
                contact_point=[contact.window convertPoint:CGPointMake(pointer.data.pointer.x_fixed/256.0,pointer.data.pointer.y_fixed/256.0) fromWindow:nil];
                UITouchPhase phase=pointer.data.pointer.phase==ARLINUX_IOS_POINTER_MOVE ? UITouchPhaseMoved :
                    pointer.data.pointer.phase==ARLINUX_IOS_POINTER_UP ? UITouchPhaseEnded : UITouchPhaseCancelled;
                deliver_contact(phase,pointer.nanoseconds);
            }
        }
    }
}

static UIWindow *key_window;
UIWindow *arlinux_ios_input_key_window(void) { return key_window; }
static void update_key_window(void)
{
    UIWindow *current=[UIApplication sharedApplication].keyWindow;
    if (current==key_window) return;
    [current retain]; [key_window release]; key_window=current;
    fprintf(stderr,"IOS_INPUT_KEY_WINDOW present=%d class=%s\n",
        current!=nil,current ? class_getName(object_getClass(current)) : "<absent>");
}
static void local_focus_update(id receiver,SEL selector,id notification)
{
    (void)receiver; (void)selector; (void)notification;
    update_key_window();
}
static void local_keyboard_start(id receiver,SEL selector)
{
    (void)receiver; (void)selector;
    update_key_window();
    fprintf(stderr,"IOS_INPUT_KEYBOARD_BACKEND backend=linux remote_service=false text_input_route=UIKeyInput\n");
}
void arlinux_ios_input_start(const struct arlinux_ios_platform_api *api)
{
    input_host=api;
    void *graphics=dlopen("/System/Library/PrivateFrameworks/GraphicsServices.framework/GraphicsServices",RTLD_NOW|RTLD_LOCAL);
    set_keyboard_attached=graphics ? (void *)dlsym(graphics,"GSEventSetHardwareKeyboardAttached") : NULL;
    if(!set_keyboard_attached) {fprintf(stderr,"IOS_INPUT_BINDING_FAILED operation=keyboard-device\n");abort();}
    Class keyboard=objc_getClass("UIKeyboard");
    require_input_method(keyboard ? object_getClass(keyboard) : Nil,"isInHardwareKeyboardMode","B16@0:8");
    method_setImplementation(class_getClassMethod(keyboard,sel_registerName("isInHardwareKeyboardMode")),(IMP)host_keyboard_mode);
    struct arlinux_ios_window_state initial;
    if(!api->read_window(&initial,sizeof(initial)))update_keyboard_device(&initial);
    Class imageCache=objc_getClass("TIImageCacheClient");
    require_input_method(imageCache,"initWithLocalAccess:","@20@0:8B16");
    Method imageInit=class_getInstanceMethod(imageCache,sel_registerName("initWithLocalAccess:"));
    original_image_cache_init=(void *)method_setImplementation(imageInit,(IMP)image_cache_without_apple_disk);
    Class cls=objc_getClass("_UIRemoteKeyboards");
    const struct { const char *selector,*types; IMP implementation; } bindings[]={
        {"applicationKeyWindowDidChange:","v24@0:8@16",(IMP)local_focus_update},
        {"startConnection","v16@0:8",(IMP)local_keyboard_start}
    };
    for (unsigned int i=0;i<sizeof(bindings)/sizeof(*bindings);i++) {
        Method method=cls ? class_getInstanceMethod(cls,sel_registerName(bindings[i].selector)) : NULL;
        const char *types=method ? method_getTypeEncoding(method) : NULL;
        if (!types || strcmp(types,bindings[i].types)) {
            fprintf(stderr,"IOS_INPUT_BINDING_FAILED selector=%s actual=%s\n",
                bindings[i].selector,types ? types : "<absent>");
            abort();
        }
        method_setImplementation(method,bindings[i].implementation);
    }
    CFRunLoopTimerRef timer=CFRunLoopTimerCreate(NULL,CFAbsoluteTimeGetCurrent()+0.016,
        0.016,0,0,input_tick,NULL);
    if(!timer)abort();
    CFRunLoopAddTimer(CFRunLoopGetMain(),timer,kCFRunLoopCommonModes);CFRelease(timer);
}
