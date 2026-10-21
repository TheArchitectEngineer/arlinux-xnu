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
        (size_t)width*4,colors,kCGImageAlphaPremultipliedFirst|kCGBitmapByteOrder32Little) : NULL;
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
    configure_web_surface_limit();
}
