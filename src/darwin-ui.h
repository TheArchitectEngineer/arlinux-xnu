/* Public CoreFoundation/QuartzCore software output on the genuine guest main
 * run loop. All Objective-C calls use fixed-argument ARM64 signatures, not
 * Linux variadic calling conventions. No host libc/TLS is used in callbacks. */
#pragma once
/* Exactly one platform state belongs to one guest address space. Host Wayland
 * state and session/peer state live elsewhere; none is a rendering singleton
 * shared between application processes. */
static struct native_ui_state {
    struct darwin_ui_api api;
    void *pixels,*context[2],*color_space;
    struct darwin_frame_control *control;
    uint32_t width,height,sequence;
    int notify,observer,drawing;
    uint32_t pid,incarnation;
    char capture_path[1024];
} ui={.notify=-1};
enum { NATIVE_CG_ALPHA_NONE_SKIP_FIRST=6, NATIVE_CG_BYTE_ORDER_32_LITTLE=2U<<12 };
extern void darwin_objc_rect(void *object,void *selector,uint64_t message,uint64_t output[4]);
extern void darwin_ui_context_flip(void *context,uint64_t translate,uint64_t scale,uint32_t height);
void darwin_native_ui_api(const struct darwin_ui_api *api) { ui.api=*api; }
static void *native_ui_selector(const char *name)
{ return ((void *(*)(const char *))(uintptr_t)ui.api.selector)(name); }
static void *native_ui_object(void *object,const char *selector)
{ return ((void *(*)(void *,void *))(uintptr_t)ui.api.message)(object,native_ui_selector(selector)); }
#include "diagnostics/ios-ui.h"
void darwin_native_ui_output(void *pixels,uint32_t width,uint32_t height,
                            struct darwin_frame_control *control,int notify)
{
    ui.pixels=pixels; ui.width=width; ui.height=height;
    ui.control=control; ui.notify=notify;
    if (darwin_native_host_identity(&ui.pid,&ui.incarnation)) {
        ui.pid=0; ui.incarnation=0;
    }
    /* Called before guest entry, while the host's own libc/TLS is active. */
    const char *directory=getenv("ARLINUX_CAPTURE_DIR");
    if (directory) {
        int count=snprintf(ui.capture_path,sizeof(ui.capture_path),"%s/frame-%ld.bgra",directory,(long)getpid());
        if (count<0 || count>=(int)sizeof(ui.capture_path)) ui.capture_path[0]=0;
    }
}
static void native_ui_frame(void *observer,uint64_t activity,void *info)
{
    (void)observer; (void)activity; (void)info;
    if (ui.drawing || !ui.control || !ipc_display_live()) return;
    ui.drawing=1;
    void *cls=((void *(*)(const char *))(uintptr_t)ui.api.objc_class)("UIApplication");
    void *app=cls ? native_ui_object(cls,"sharedApplication") : NULL;
    void *windows=app ? native_ui_object(app,"windows") : NULL;
    uint64_t count=windows ? ((uint64_t (*)(void *,void *))(uintptr_t)ui.api.message)(windows,native_ui_selector("count")) : 0;
    if (!count || count>64) { ui.drawing=0; return; }
    for (unsigned int slot=0;slot<2;slot++) {
        uint32_t expected=D_FRAME_FREE;
        if (!__atomic_compare_exchange_n(ui.control->state+slot,&expected,D_FRAME_DRAWING,0,__ATOMIC_ACQ_REL,__ATOMIC_RELAXED)) continue;
        unsigned long bytes=(unsigned long)ui.width*ui.height*4;
        void *pixels=(unsigned char *)ui.pixels+slot*bytes;
        if (!ui.color_space) {
            trace_record("UI_RENDER_BEGIN windows=0x",count,ui.width,ui.height);
            ui.color_space=((void *(*)(void))(uintptr_t)ui.api.color_space)();
        }
        if (!ui.context[slot] && ui.color_space) {
            ui.context[slot]=((void *(*)(void *,uint64_t,uint64_t,uint64_t,uint64_t,void *,uint32_t))(uintptr_t)ui.api.bitmap_create)
                (pixels,ui.width,ui.height,8,ui.width*4,ui.color_space,
                 NATIVE_CG_BYTE_ORDER_32_LITTLE|NATIVE_CG_ALPHA_NONE_SKIP_FIRST);
            /* UIKit layers use a downward Y axis; bitmap CG contexts do not.
             * Apply it once per context, not once per frame. */
            if (ui.context[slot]) darwin_ui_context_flip(ui.context[slot],
                ui.api.context_translate,ui.api.context_scale,ui.height);
        }
        if (!ui.context[slot]) {
            __atomic_store_n(ui.control->state+slot,D_FRAME_FREE,__ATOMIC_RELEASE);
            trace_record("UI_BITMAP_CONTEXT_UNAVAILABLE slot=0x",slot,0,0);
            break;
        }
        volatile uint32_t *clear=pixels;
        for (unsigned long i=0;i<bytes/4;i++) clear[i]=0;
        unsigned int rendered=0;
        for (uint64_t i=0;i<count;i++) {
            void *window=((void *(*)(void *,void *,uint64_t))(uintptr_t)ui.api.message)(windows,native_ui_selector("objectAtIndex:"),i);
            if (((signed char (*)(void *,void *))(uintptr_t)ui.api.message)(window,native_ui_selector("isHidden"))) continue;
            void *layer=native_ui_object(window,"layer");
            if (!layer) continue;
            native_ui_layout_description(window);
            if (ui.sequence<2) {
                native_ui_geometry(window,"UI_WINDOW_BOUNDS object=0x");
                native_ui_geometry(layer,"UI_LAYER_BOUNDS object=0x");
                void *children=native_ui_object(layer,"sublayers");
                uint64_t child_count=children ? ((uint64_t (*)(void *,void *))(uintptr_t)ui.api.message)(children,native_ui_selector("count")) : 0;
                trace_record("UI_LAYER_CHILDREN count=0x",child_count,(uintptr_t)layer,(uintptr_t)children);
            }
            ((void (*)(void *,void *,void *))(uintptr_t)ui.api.message)(layer,native_ui_selector("renderInContext:"),ui.context[slot]);
            rendered++;
        }
        if (rendered) {
            native_ui_capture(pixels,bytes);
            ui.control->sequence[slot]=++ui.sequence;
            __atomic_store_n(ui.control->state+slot,D_FRAME_READY,__ATOMIC_RELEASE);
            uint64_t token=1;
            long result=RAW(SYS_write,ui.notify,&token,sizeof(token),0,0,0);
            trace_record("UI_LAYER_FRAME sequence=0x",ui.sequence,rendered,result);
        } else __atomic_store_n(ui.control->state+slot,D_FRAME_FREE,__ATOMIC_RELEASE);
        break;
    }
    ui.drawing=0;
}
static long native_ios_platform_attach(void)
{
    if (ui.observer) return 0;
    if (!ui.pixels || !ui.control || native_current_thread()!=native_threads) return -EINVAL;
    if (!darwin_ui_api_ready(&ui.api)) {
        trace_record("UI_PLATFORM_IMPORTS_UNAVAILABLE status=0x",D_RUNTIME_FATAL_EXIT,0,0);
        return -ENOTSUP;
    }
    void *loop=((void *(*)(void))(uintptr_t)ui.api.runloop_main)();
    void *observer=((void *(*)(void *,uint64_t,_Bool,int64_t,void (*)(void *,uint64_t,void *),void *))
        (uintptr_t)ui.api.observer_create)(NULL,1U<<5,1,1U<<30,native_ui_frame,NULL);
    if (!loop || !observer) { trace_record("UI_RUNLOOP_OBSERVER_UNAVAILABLE loop=0x",(uintptr_t)loop,(uintptr_t)observer,0); return -ENOTSUP; }
    ((void (*)(void *,void *,void *))(uintptr_t)ui.api.observer_add)(loop,observer,(void *)(uintptr_t)ui.api.common_modes);
    ui.observer=1;
    const char marker[]="UI_RUNLOOP_OBSERVER_INSTALLED genuine main run loop, before waiting\n";
    RAW(SYS_write,native_trace_fd,marker,sizeof(marker)-1,0,0,0);
    return 0;
}
static long native_ios_window_read(struct arlinux_ios_window_state *state,unsigned long size)
{
    if (!state || size!=sizeof(*state)) return -EINVAL;
    if (!ui.control || !ui.pid) return -ENOTSUP;
    /* The backend can change state while the guest runs. Retry a bounded
     * number of times; do not spin forever after a writer exits mid-update. */
    for (unsigned int i=0;i<16;i++) {
        uint32_t before=__atomic_load_n(&ui.control->window_sequence,__ATOMIC_SEQ_CST);
        if (before&1) continue;
        struct arlinux_ios_window_state value={.generation=before,.pid=ui.pid,.incarnation=ui.incarnation};
        value.width=__atomic_load_n(&ui.control->width,__ATOMIC_RELAXED);
        value.height=__atomic_load_n(&ui.control->height,__ATOMIC_RELAXED);
        value.refresh_millihz=__atomic_load_n(&ui.control->refresh_millihz,__ATOMIC_RELAXED);
        value.flags=__atomic_load_n(&ui.control->window_flags,__ATOMIC_RELAXED);
        if (before!=__atomic_load_n(&ui.control->window_sequence,__ATOMIC_SEQ_CST)) continue;
        /* A SIGKILL/abort cannot publish the child's final shared-memory
         * update. Kernel child status remains authoritative for liveness. */
        if (!ipc_display_live()) value.flags&=~(ARLINUX_IOS_WINDOW_BACKEND_ALIVE|ARLINUX_IOS_WINDOW_ACTIVE);
        *state=value; return 0;
    }
    return -EAGAIN;
}
static long native_ios_input_read(struct arlinux_ios_input_event *event,unsigned long size)
{
    if(!event || size!=sizeof(*event))return -EINVAL;
    if(!ui.control || native_current_thread()!=native_threads)return -ENOTSUP;
    uint32_t read=__atomic_load_n(&ui.control->input_read,__ATOMIC_RELAXED);
    uint32_t written=__atomic_load_n(&ui.control->input_write,__ATOMIC_ACQUIRE);
    if(__atomic_exchange_n(&ui.control->input_overflow,0,__ATOMIC_ACQ_REL)) {
        __atomic_store_n(&ui.control->input_read,written,__ATOMIC_RELEASE);
        return -EOVERFLOW;
    }
    if(read==written)return 0;
    *event=ui.control->input[read%ARLINUX_IOS_INPUT_CAPACITY];
    __atomic_store_n(&ui.control->input_read,read+1,__ATOMIC_RELEASE);
    return 1;
}
static const struct arlinux_ios_platform_api native_ios_platform_api={
    .version=ARLINUX_IOS_PLATFORM_ABI_VERSION,.size=sizeof(struct arlinux_ios_platform_api),
    .attach=native_ios_platform_attach,.read_window=native_ios_window_read,
    .read_input=native_ios_input_read
};
