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
static void key_window_changed(id receiver,SEL selector,id notification)
{
    (void)receiver; (void)selector; (void)notification;
    UIWindow *current=[UIApplication sharedApplication].keyWindow;
    if (current==key_window) return;
    [current retain]; [key_window release]; key_window=current;
    fprintf(stderr,"IOS_INPUT_KEY_WINDOW present=%d class=%s input_delivery=false\n",
        current!=nil,current ? class_getName(object_getClass(current)) : "<absent>");
}
void arlinux_ios_input_start(void)
{
    Class cls=objc_getClass("_UIRemoteKeyboards");
    Method method=cls ? class_getInstanceMethod(cls,sel_registerName("applicationKeyWindowDidChange:")) : NULL;
    const char *types=method ? method_getTypeEncoding(method) : NULL;
    if (!types || strcmp(types,"v24@0:8@16")) {
        fprintf(stderr,"IOS_INPUT_BINDING_FAILED selector=applicationKeyWindowDidChange: actual=%s\n",
            types ? types : "<absent>");
        abort();
    }
    method_setImplementation(method,(IMP)key_window_changed);
}
