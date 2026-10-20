/* UIKit key-window ownership for the Linux input backend. Apple remote-keyboard
 * focus IPC cannot export a local scene token. Keep the actual UIKit key window
 * locally instead. This does not implement host input or an on-screen keyboard;
 * those capabilities must still be verified through external app interaction.
 */
#import <UIKit/UIKit.h>
#import <objc/runtime.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static UIWindow *key_window;
UIWindow *arlinux_ios_input_key_window(void) { return key_window; }
static void update_key_window(void)
{
    UIWindow *current=[UIApplication sharedApplication].keyWindow;
    if (current==key_window) return;
    [current retain]; [key_window release]; key_window=current;
    fprintf(stderr,"IOS_INPUT_KEY_WINDOW present=%d class=%s input_delivery=false\n",
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
    fprintf(stderr,"IOS_INPUT_KEYBOARD_BACKEND backend=linux remote_service=false input_delivery=false\n");
}
void arlinux_ios_input_start(void)
{
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
}
