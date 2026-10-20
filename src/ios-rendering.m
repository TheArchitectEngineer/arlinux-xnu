/* Software-window platform policy. Preserve Apple's CAContext/CALayer object
 * semantics, but use its in-process context constructor instead of connecting
 * UIWindow to an absent Apple render server. Actual layer pixels are rendered
 * by the existing CG/Wayland backend; context creation is not presentation.
 * Remote context sharing and hardware compositor services are not supplied.
 */
#import <Foundation/Foundation.h>
#import <objc/runtime.h>
#import <QuartzCore/QuartzCore.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static id (*local_context)(id,SEL,id);
static SEL local_selector;
static void software_layer_display(CALayer *layer,SEL selector)
{
    (void)selector;
    CGRect bounds=layer.bounds;
    CGFloat scale=layer.contentsScale;
    if (CGRectIsEmpty(bounds)) { layer.contents=nil; return; }
    double width=ceil(bounds.size.width*scale),height=ceil(bounds.size.height*scale);
    if (!isfinite(width) || !isfinite(height) || !isfinite(bounds.origin.x) || !isfinite(bounds.origin.y) ||
        scale<=0 || width<1 || height<1 || width>8192 || height>8192) {
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
        [layer drawInContext:context];
        image=CGBitmapContextCreateImage(context);
        if (!image) { fprintf(stderr,"IOS_RENDER_CPU_BACKING_FAILED reason=image-snapshot\n"); abort(); }
        layer.contents=(id)image;
        static unsigned int logged;
        if (logged++<8) fprintf(stderr,"IOS_RENDER_CPU_BACKING class=%s width=%zu height=%zu app_acceptance=false\n",
            class_getName(object_getClass(layer)),CGImageGetWidth(image),CGImageGetHeight(image));
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
}
