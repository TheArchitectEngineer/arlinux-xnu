/* Software-window platform policy. Preserve Apple's CAContext/CALayer object
 * semantics, but use its in-process context constructor instead of connecting
 * UIWindow to an absent Apple render server. Actual layer pixels are rendered
 * by the existing CG/Wayland backend; context creation is not presentation.
 * Remote context sharing and hardware compositor services are not supplied.
 */
#import <Foundation/Foundation.h>
#import <UIKit/UIKit.h>
#import <objc/runtime.h>
#import <QuartzCore/QuartzCore.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include "ios-platform-abi.h"

static id (*local_context)(id,SEL,id);
static SEL local_selector;
static void software_layer_display(CALayer *layer,SEL selector)
{
    (void)selector;
    id<CALayerDelegate> delegate=layer.delegate;
    if ([delegate respondsToSelector:@selector(layerWillDraw:)]) [delegate layerWillDraw:layer];
    CGRect bounds=layer.bounds;
    CGFloat scale=layer.contentsScale;
    if (CGRectIsEmpty(bounds)) { layer.contents=nil; return; }
    double width=ceil(bounds.size.width*scale),height=ceil(bounds.size.height*scale);
    if (!isfinite(width) || !isfinite(height) || !isfinite(bounds.origin.x) || !isfinite(bounds.origin.y) ||
        scale<=0 || width<1 || height<1 || width>ARLINUX_IOS_SOFTWARE_SURFACE_MAX ||
        height>ARLINUX_IOS_SOFTWARE_SURFACE_MAX) {
        fprintf(stderr,"IOS_RENDER_CPU_BACKING_FAILED reason=unsupported-geometry width=%g height=%g scale=%g\n",width,height,(double)scale);
        abort();
    }
    /* Own only bitmap storage. The genuine drawInContext: implementation and
     * its UIKit/delegate drawing still produce all content. Normal CALayer
     * display scheduling and delegate displayLayer: dispatch stay original. */
    CGColorSpaceRef colors=CGColorSpaceCreateDeviceRGB();
    CGContextRef context=colors ? CGBitmapContextCreate(NULL,(size_t)width,(size_t)height,8,
        (size_t)width*ARLINUX_IOS_SOFTWARE_PIXEL_BYTES,colors,kCGImageAlphaPremultipliedFirst|kCGBitmapByteOrder32Little) : NULL;
    if (colors) CGColorSpaceRelease(colors);
    if (!context) {
        fprintf(stderr,"IOS_RENDER_CPU_BACKING_FAILED reason=bitmap-allocation\n"); abort();
    }
    CGImageRef image=NULL;
    @try {
        CGContextTranslateCTM(context,0,height);
        CGContextScaleCTM(context,scale,-scale);
        CGContextTranslateCTM(context,-bounds.origin.x,-bounds.origin.y);
        void (^draw)(void)=^{
            UIGraphicsPushContext(context);
            @try { [layer drawInContext:context]; }
            @finally { UIGraphicsPopContext(); }
        };
        /* Dynamic UIKit drawing resolves against the real view's traits and
         * current graphics context. Supply that normal drawing environment,
         * never substitute a color or modify the view's trait collection. */
        UITraitCollection *traits=[delegate respondsToSelector:@selector(traitCollection)] ?
            [(id<UITraitEnvironment>)delegate traitCollection] : nil;
        if (traits) [traits performAsCurrentTraitCollection:draw];
        else draw();
        image=CGBitmapContextCreateImage(context);
        if (!image) { fprintf(stderr,"IOS_RENDER_CPU_BACKING_FAILED reason=image-snapshot\n"); abort(); }
        layer.contents=(id)image;
        static unsigned int logged;
        if (logged++<8) {
            size_t nonzero=0;uint32_t first=0;
            const uint32_t *pixels=CGBitmapContextGetData(context);
            for (size_t i=0;i<(size_t)width*(size_t)height;i++) if (pixels[i]>>24) {
                if (!nonzero) first=pixels[i];
                nonzero++;
            }
            fprintf(stderr,"IOS_RENDER_CPU_BACKING class=%s width=%zu height=%zu alpha_pixels=%zu first_pixel=%08x app_acceptance=false\n",
                class_getName(object_getClass(layer)),CGImageGetWidth(image),CGImageGetHeight(image),nonzero,first);
        }
    } @finally {
        if (image) CGImageRelease(image);
        CGContextRelease(context);
    }
}
static id software_context(id cls,SEL selector,id options)
{
    (void)selector;
    id context=local_context(cls,local_selector,options);
    fprintf(stderr,"IOS_RENDER_LOCAL_CONTEXT class=%s created=%d app_acceptance=false\n",
        context ? class_getName(object_getClass(context)) : "<absent>",context!=nil);
    if (!context) abort();
    return context;
}
/* This backend has CPU bitmaps, not Apple's IOSurface kernel service. Select
 * WebKit's own rendering policy before it creates a page. Keep HTML/JS/layout,
 * framework callbacks and the app's configuration object original. */
static id (*original_web_init)(id,SEL,CGRect,id);
static BOOL web_signature(Method method,const char *result,unsigned count,const char *arg2,const char *arg3)
{
    if(!method || method_getNumberOfArguments(method)!=count)return NO;
    char type[96];method_getReturnType(method,type,sizeof(type));
    if(strcmp(type,result))return NO;
    if(arg2){method_getArgumentType(method,2,type,sizeof(type));if(strcmp(type,arg2))return NO;}
    if(arg3){method_getArgumentType(method,3,type,sizeof(type));if(strcmp(type,arg3))return NO;}
    return YES;
}
static void web_software_preferences(id configuration)
{
    if(!configuration)return;
    SEL preferences_selector=sel_registerName("preferences");
    Method preferences=class_getInstanceMethod(object_getClass(configuration),preferences_selector);
    if(!web_signature(preferences,"@",2,NULL,NULL))return;
    id prefs=((id (*)(id,SEL))method_getImplementation(preferences))(configuration,preferences_selector);
    Class cls=object_getClass(prefs);
    SEL features_selector=sel_registerName("_features");
    SEL set_selector=sel_registerName("_setEnabled:forFeature:");
    SEL get_selector=sel_registerName("_isEnabledForFeature:");
    SEL key_selector=sel_registerName("key");
    Method features=class_getClassMethod(cls,features_selector);
    Method set=class_getInstanceMethod(cls,set_selector),get=class_getInstanceMethod(cls,get_selector);
    if(!web_signature(features,"@",2,NULL,NULL) ||
       !web_signature(set,"v",4,"B","@") || !web_signature(get,"B",3,"@",NULL)) {
        fprintf(stderr,"IOS_WEB_CPU_POLICY_UNAVAILABLE reason=feature-interface\n");return;
    }
    NSArray *items=((id (*)(id,SEL))method_getImplementation(features))(cls,features_selector);
    if(![items isKindOfClass:[NSArray class]] || items.count>2048) {
        fprintf(stderr,"IOS_WEB_CPU_POLICY_UNAVAILABLE reason=feature-collection\n");return;
    }
    unsigned selected=0;
    for(id feature in items) {
        Method key=class_getInstanceMethod(object_getClass(feature),key_selector);
        if(!web_signature(key,"@",2,NULL,NULL))continue;
        NSString *name=((id (*)(id,SEL))method_getImplementation(key))(feature,key_selector);
        if(![name isKindOfClass:[NSString class]])continue;
        if(![name isEqualToString:@"UseGPUProcessForDOMRenderingEnabled"] &&
           ![name isEqualToString:@"UseGPUProcessForCanvasRenderingEnabled"] &&
           ![name isEqualToString:@"AcceleratedDrawingEnabled"])continue;
        BOOL before=((BOOL (*)(id,SEL,id))method_getImplementation(get))(prefs,get_selector,feature);
        ((void (*)(id,SEL,BOOL,id))method_getImplementation(set))(prefs,set_selector,NO,feature);
        BOOL after=((BOOL (*)(id,SEL,id))method_getImplementation(get))(prefs,get_selector,feature);
        fprintf(stderr,"IOS_WEB_CPU_POLICY key=%s before=%d after=%d app_acceptance=false\n",
            name.UTF8String,before,after);
        if(after)abort();selected++;
    }
    if(!selected)fprintf(stderr,"IOS_WEB_CPU_POLICY_UNAVAILABLE reason=no-rendering-features\n");
}
static id software_web_init(id view,SEL selector,CGRect frame,id configuration)
{
    web_software_preferences(configuration);
    return original_web_init(view,selector,frame,configuration);
}
static void configure_web_rendering(void)
{
    Class cls=objc_getClass("WKWebView");if(!cls)return;
    Method method=class_getInstanceMethod(cls,sel_registerName("initWithFrame:configuration:"));
    if(!web_signature(method,"@",4,"{CGRect={CGPoint=dd}{CGSize=dd}}","@")) {
        fprintf(stderr,"IOS_WEB_CPU_POLICY_UNAVAILABLE reason=view-interface\n");return;
    }
    original_web_init=(void *)method_getImplementation(method);
    method_setImplementation(method,(IMP)software_web_init);
}
static void configure_web_surface_limit(void)
{
    if(!objc_getClass("WKWebView"))return;
    /* WebCore already provides this platform configuration interface, also
     * used when its WebContent peer receives the UI process's maximum size.
     * The measured iOS 16.7.16 ABI stores an IntSize (two int32s) from x0;
     * its own maximumSize() reads that atomic value before consulting IOKit.
     * Configure the real CPU backing limit, without inventing an IOSurface
     * service, successful allocation, GPU resource or application effect. */
    struct webcore_size { int32_t width,height; };
    void *webcore=dlopen("/System/Library/PrivateFrameworks/WebCore.framework/WebCore",RTLD_NOW|RTLD_LOCAL|RTLD_NOLOAD);
    void (*set_size)(struct webcore_size)=webcore ?
        dlsym(webcore,"_ZN7WebCore9IOSurface14setMaximumSizeENS_7IntSizeE") : NULL;
    struct webcore_size (*get_size)(void)=webcore ?
        dlsym(webcore,"_ZN7WebCore9IOSurface11maximumSizeEv") : NULL;
    void (*set_alignment)(size_t)=webcore ?
        dlsym(webcore,"_ZN7WebCore9IOSurface23setBytesPerRowAlignmentEm") : NULL;
    size_t (*get_alignment)(void)=webcore ?
        dlsym(webcore,"_ZN7WebCore9IOSurface20bytesPerRowAlignmentEv") : NULL;
    if(!set_size || !get_size) {
        fprintf(stderr,"IOS_WEB_SURFACE_LIMIT_UNAVAILABLE setter=%d getter=%d\n",set_size!=NULL,get_size!=NULL);
        if(webcore)dlclose(webcore);
        return; /* Keep the original unsupported path observable. */
    }
    struct webcore_size limit={ARLINUX_IOS_SOFTWARE_SURFACE_MAX,ARLINUX_IOS_SOFTWARE_SURFACE_MAX};
    set_size(limit);
    struct webcore_size actual=get_size();
    fprintf(stderr,"IOS_WEB_SURFACE_LIMIT width=%d height=%d backend=cpu app_acceptance=false\n",
        actual.width,actual.height);
    if(actual.width!=limit.width || actual.height!=limit.height)abort();
    if(set_alignment && get_alignment) {
        /* The real bitmap allocator takes width * four BGRA bytes per row;
         * no driver pitch or unavailable GPU alignment is advertised. */
        set_alignment(ARLINUX_IOS_SOFTWARE_PIXEL_BYTES);
        size_t alignment=get_alignment();
        fprintf(stderr,"IOS_WEB_SURFACE_LAYOUT row_alignment=%zu pixel_bytes=%u backend=cpu app_acceptance=false\n",
            alignment,ARLINUX_IOS_SOFTWARE_PIXEL_BYTES);
        if(alignment!=ARLINUX_IOS_SOFTWARE_PIXEL_BYTES)abort();
    } else fprintf(stderr,"IOS_WEB_SURFACE_LAYOUT_UNAVAILABLE setter=%d getter=%d\n",
        set_alignment!=NULL,get_alignment!=NULL);
    dlclose(webcore);
}
void arlinux_ios_rendering_start(void)
{
    Class cls=objc_getClass("CAContext");
    Method remote=cls ? class_getClassMethod(cls,sel_registerName("remoteContextWithOptions:")) : NULL;
    local_selector=sel_registerName("localContextWithOptions:");
    Method local=cls ? class_getClassMethod(cls,local_selector) : NULL;
    const char *remote_types=remote ? method_getTypeEncoding(remote) : NULL;
    const char *local_types=local ? method_getTypeEncoding(local) : NULL;
    if (!remote_types || !local_types || strcmp(remote_types,"@24@0:8@16") || strcmp(local_types,"@24@0:8@16")) {
        fprintf(stderr,"IOS_RENDER_BINDING_FAILED remote=%s local=%s\n",
            remote_types ? remote_types : "<absent>",local_types ? local_types : "<absent>");
        abort();
    }
    local_context=(void *)method_getImplementation(local);
    method_setImplementation(remote,(IMP)software_context);
    Method display=class_getInstanceMethod([CALayer class],sel_registerName("_display"));
    const char *display_types=display ? method_getTypeEncoding(display) : NULL;
    if (!display_types || strcmp(display_types,"v16@0:8")) {
        fprintf(stderr,"IOS_RENDER_BINDING_FAILED selector=CALayer._display actual=%s\n",display_types ? display_types : "<absent>");
        abort();
    }
    method_setImplementation(display,(IMP)software_layer_display);
    configure_web_rendering();
    configure_web_surface_limit();
}
