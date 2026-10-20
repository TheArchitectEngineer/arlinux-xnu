/* Public CoreFoundation/QuartzCore software output on the genuine guest main
 * run loop. All Objective-C calls use fixed-argument ARM64 signatures, not
 * Linux variadic calling conventions. No host libc/TLS is used in callbacks. */
#pragma once
static struct darwin_ui_api native_ui_api;
static void *native_ui_pixels,*native_ui_context[2],*native_ui_color_space;
static struct darwin_frame_control *native_ui_control;
static uint32_t native_ui_width,native_ui_height,native_ui_sequence;
static int native_ui_notify=-1,native_ui_observer,native_ui_drawing;
static char native_ui_capture_path[1024];
enum { NATIVE_CG_ALPHA_NONE_SKIP_FIRST=6, NATIVE_CG_BYTE_ORDER_32_LITTLE=2U<<12 };
extern void darwin_objc_rect(void *object,void *selector,uint64_t message,uint64_t output[4]);
extern void darwin_ui_context_flip(void *context,uint64_t translate,uint64_t scale,uint32_t height);
void darwin_native_ui_api(const struct darwin_ui_api *api) { native_ui_api=*api; }
void darwin_native_ui_output(void *pixels,uint32_t width,uint32_t height,
                            struct darwin_frame_control *control,int notify)
{
    native_ui_pixels=pixels; native_ui_width=width; native_ui_height=height;
    native_ui_control=control; native_ui_notify=notify;
    /* Called before guest entry, while the host's own libc/TLS is active. */
    const char *directory=getenv("ARLINUX_CAPTURE_DIR");
    if (directory) {
        int count=snprintf(native_ui_capture_path,sizeof(native_ui_capture_path),"%s/frame-%ld.bgra",directory,(long)getpid());
        if (count<0 || count>=(int)sizeof(native_ui_capture_path)) native_ui_capture_path[0]=0;
    }
}
static void *native_ui_selector(const char *name)
{ return ((void *(*)(const char *))(uintptr_t)native_ui_api.selector)(name); }
static void native_ui_geometry(void *object,const char *tag)
{
    uint64_t rect[4];
    darwin_objc_rect(object,native_ui_selector("bounds"),native_ui_api.message,rect);
    trace_record(tag,(uintptr_t)object,rect[0],rect[1]);
    trace_record("UI_BOUNDS_SIZE width=0x",rect[2],rect[3],0);
}
static void *native_ui_object(void *object,const char *selector)
{ return ((void *(*)(void *,void *))(uintptr_t)native_ui_api.message)(object,native_ui_selector(selector)); }
static void native_ui_layout_description(void *window)
{
    /* Explicit runner evidence only. Read UIKit's own diagnostic view tree
     * on its normal main run loop; never invoke control actions or alter layout.
     * Limit text emitted by this runtime, and never call host stdio with guest TLS.
     */
    if (!native_ui_capture_path[0] || native_ui_sequence!=1) return;
    void *selector=native_ui_selector("recursiveDescription");
    if (!((signed char (*)(void *,void *,void *))(uintptr_t)native_ui_api.message)
            (window,native_ui_selector("respondsToSelector:"),selector)) return;
    void *description=((void *(*)(void *,void *))(uintptr_t)native_ui_api.message)(window,selector);
    const char *text=description ? native_ui_object(description,"UTF8String") : NULL;
    if (!text) return;
    unsigned long length=0; while (length<8192 && text[length]) length++;
    const char marker[]="UI_VIEW_TREE genuine UIKit recursiveDescription follows\n";
    RAW(SYS_write,2,marker,sizeof(marker)-1,0,0,0);
    RAW(SYS_write,2,text,length,0,0,0);
    RAW(SYS_write,2,"\n",1,0,0,0);
}
static void native_ui_capture(void *pixels,unsigned long bytes)
{
    if (!native_ui_capture_path[0]) return;
    /* Retain the last completed render, not only a possibly empty first frame. */
    long fd=RAW(SYS_openat,AT_FDCWD,native_ui_capture_path,O_WRONLY|O_CREAT|O_TRUNC|O_CLOEXEC,0600,0,0);
    if (fd<0) { trace_record("UI_FRAME_CAPTURE_ERROR fd=0x",fd,0,0); return; }
    uint32_t header[3]={native_ui_width,native_ui_height,native_ui_width*4};
    unsigned long header_offset=0;
    long written=0;
    while (header_offset<sizeof(header)) {
        written=RAW(SYS_write,fd,(unsigned char *)header+header_offset,sizeof(header)-header_offset,0,0,0);
        if (written==-EINTR) continue;
        if (written<=0) break;
        header_offset+=written;
    }
    unsigned long offset=0;
    while (header_offset==sizeof(header) && offset<bytes) {
        written=RAW(SYS_write,fd,(unsigned char *)pixels+offset,bytes-offset,0,0,0);
        if (written==-EINTR) continue;
        if (written<=0) break;
        if (written>0) offset+=written;
    }
    RAW(SYS_close,fd,0,0,0,0,0);
    trace_record("UI_FRAME_CAPTURE bytes=0x",offset,bytes,written<0 ? written : 0);
}
static void native_ui_frame(void *observer,uint64_t activity,void *info)
{
    (void)observer; (void)activity; (void)info;
    if (native_ui_drawing || !native_ui_control || !ipc_display_live()) return;
    native_ui_drawing=1;
    void *cls=((void *(*)(const char *))(uintptr_t)native_ui_api.objc_class)("UIApplication");
    void *app=cls ? native_ui_object(cls,"sharedApplication") : NULL;
    void *windows=app ? native_ui_object(app,"windows") : NULL;
    uint64_t count=windows ? ((uint64_t (*)(void *,void *))(uintptr_t)native_ui_api.message)(windows,native_ui_selector("count")) : 0;
    if (!count || count>64) { native_ui_drawing=0; return; }
    for (unsigned int slot=0;slot<2;slot++) {
        uint32_t expected=D_FRAME_FREE;
        if (!__atomic_compare_exchange_n(native_ui_control->state+slot,&expected,D_FRAME_DRAWING,0,__ATOMIC_ACQ_REL,__ATOMIC_RELAXED)) continue;
        unsigned long bytes=(unsigned long)native_ui_width*native_ui_height*4;
        void *pixels=(unsigned char *)native_ui_pixels+slot*bytes;
        if (!native_ui_color_space) {
            trace_record("UI_RENDER_BEGIN windows=0x",count,native_ui_width,native_ui_height);
            native_ui_color_space=((void *(*)(void))(uintptr_t)native_ui_api.color_space)();
        }
        if (!native_ui_context[slot] && native_ui_color_space) {
            native_ui_context[slot]=((void *(*)(void *,uint64_t,uint64_t,uint64_t,uint64_t,void *,uint32_t))(uintptr_t)native_ui_api.bitmap_create)
                (pixels,native_ui_width,native_ui_height,8,native_ui_width*4,native_ui_color_space,
                 NATIVE_CG_BYTE_ORDER_32_LITTLE|NATIVE_CG_ALPHA_NONE_SKIP_FIRST);
            /* UIKit layers use a downward Y axis; bitmap CG contexts do not.
             * Apply it once per context, not once per frame. */
            if (native_ui_context[slot]) darwin_ui_context_flip(native_ui_context[slot],
                native_ui_api.context_translate,native_ui_api.context_scale,native_ui_height);
        }
        if (!native_ui_context[slot]) {
            __atomic_store_n(native_ui_control->state+slot,D_FRAME_FREE,__ATOMIC_RELEASE);
            trace_record("UI_BITMAP_CONTEXT_UNAVAILABLE slot=0x",slot,0,0);
            break;
        }
        volatile uint32_t *clear=pixels;
        for (unsigned long i=0;i<bytes/4;i++) clear[i]=0;
        unsigned int rendered=0;
        for (uint64_t i=0;i<count;i++) {
            void *window=((void *(*)(void *,void *,uint64_t))(uintptr_t)native_ui_api.message)(windows,native_ui_selector("objectAtIndex:"),i);
            if (((signed char (*)(void *,void *))(uintptr_t)native_ui_api.message)(window,native_ui_selector("isHidden"))) continue;
            void *layer=native_ui_object(window,"layer");
            if (!layer) continue;
            native_ui_layout_description(window);
            if (native_ui_sequence<2) {
                native_ui_geometry(window,"UI_WINDOW_BOUNDS object=0x");
                native_ui_geometry(layer,"UI_LAYER_BOUNDS object=0x");
                void *children=native_ui_object(layer,"sublayers");
                uint64_t child_count=children ? ((uint64_t (*)(void *,void *))(uintptr_t)native_ui_api.message)(children,native_ui_selector("count")) : 0;
                trace_record("UI_LAYER_CHILDREN count=0x",child_count,(uintptr_t)layer,(uintptr_t)children);
            }
            ((void (*)(void *,void *,void *))(uintptr_t)native_ui_api.message)(layer,native_ui_selector("renderInContext:"),native_ui_context[slot]);
            rendered++;
        }
        if (rendered) {
            native_ui_capture(pixels,bytes);
            native_ui_control->sequence[slot]=++native_ui_sequence;
            __atomic_store_n(native_ui_control->state+slot,D_FRAME_READY,__ATOMIC_RELEASE);
            uint64_t token=1;
            long result=RAW(SYS_write,native_ui_notify,&token,sizeof(token),0,0,0);
            trace_record("UI_LAYER_FRAME sequence=0x",native_ui_sequence,rendered,result);
        } else __atomic_store_n(native_ui_control->state+slot,D_FRAME_FREE,__ATOMIC_RELEASE);
        break;
    }
    native_ui_drawing=0;
}
static void native_ui_install_observer(void)
{
    if (native_ui_observer || !native_ui_pixels || !native_ui_control ||
        !native_ui_api.runloop_main || !native_ui_api.observer_create || !native_ui_api.observer_add ||
        !native_ui_api.common_modes || !native_ui_api.objc_class || !native_ui_api.selector ||
        !native_ui_api.message || !native_ui_api.color_space || !native_ui_api.bitmap_create ||
        !native_ui_api.context_translate || !native_ui_api.context_scale ||
        native_current_thread()!=native_threads) return;
    native_ui_observer=1;
    void *loop=((void *(*)(void))(uintptr_t)native_ui_api.runloop_main)();
    void *observer=((void *(*)(void *,uint64_t,_Bool,int64_t,void (*)(void *,uint64_t,void *),void *))
        (uintptr_t)native_ui_api.observer_create)(NULL,1U<<5,1,1U<<30,native_ui_frame,NULL);
    if (!loop || !observer) { trace_record("UI_RUNLOOP_OBSERVER_UNAVAILABLE loop=0x",(uintptr_t)loop,(uintptr_t)observer,0); return; }
    ((void (*)(void *,void *,void *))(uintptr_t)native_ui_api.observer_add)(loop,observer,(void *)(uintptr_t)native_ui_api.common_modes);
    const char marker[]="UI_RUNLOOP_OBSERVER_INSTALLED genuine main run loop, before waiting\n";
    RAW(SYS_write,2,marker,sizeof(marker)-1,0,0,0);
}
