/* Linkage contract between the cache loader and the native iOS adapter.
 * ARM64 guest addresses are explicit; floating-point ABI calls use assembly
 * veneers. This header has no loader, UIKit policy or host libc operations.
 */
#pragma once
#include <stdint.h>
#define IOS_CF_IMAGE "/System/Library/Frameworks/CoreFoundation.framework/CoreFoundation"
#define IOS_CG_IMAGE "/System/Library/Frameworks/CoreGraphics.framework/CoreGraphics"
#define IOS_OBJC_IMAGE "/usr/lib/libobjc.A.dylib"
#define IOS_UI_IMPORTS(X) \
    X(IOS_CF_IMAGE,"_CFRunLoopGetMain",runloop_main,0) \
    X(IOS_CF_IMAGE,"_CFRunLoopObserverCreate",observer_create,0) \
    X(IOS_CF_IMAGE,"_CFRunLoopAddObserver",observer_add,0) \
    X(IOS_CF_IMAGE,"_kCFRunLoopCommonModes",common_modes,1) \
    X(IOS_OBJC_IMAGE,"_objc_getClass",objc_class,0) \
    X(IOS_OBJC_IMAGE,"_sel_registerName",selector,0) \
    X(IOS_OBJC_IMAGE,"_objc_msgSend",message,0) \
    X(IOS_CG_IMAGE,"_CGColorSpaceCreateDeviceRGB",color_space,0) \
    X(IOS_CG_IMAGE,"_CGBitmapContextCreate",bitmap_create,0) \
    X(IOS_CG_IMAGE,"_CGContextTranslateCTM",context_translate,0) \
    X(IOS_CG_IMAGE,"_CGContextScaleCTM",context_scale,0) \
    X(IOS_CG_IMAGE,"_CGContextSaveGState",context_save,0) \
    X(IOS_CG_IMAGE,"_CGContextRestoreGState",context_restore,0)

struct darwin_ui_api {
#define IOS_DECLARE_IMPORT(image,symbol,field,data) uint64_t field;
    IOS_UI_IMPORTS(IOS_DECLARE_IMPORT)
#undef IOS_DECLARE_IMPORT
};
static inline int darwin_ui_api_ready(const struct darwin_ui_api *api)
{
#define IOS_REQUIRE_IMPORT(image,symbol,field,data) if (!api->field) return 0;
    IOS_UI_IMPORTS(IOS_REQUIRE_IMPORT)
#undef IOS_REQUIRE_IMPORT
    return 1;
}
