/* Optional runner evidence. Included privately by the iOS adapter after its
 * TLS-safe helpers. This is not a rendering/input or acceptance implementation.
 * No symbols here are imported by the cache loader or session service.
 */
#pragma once
static void native_ui_geometry(void *object,const char *tag)
{
    uint64_t rect[4];
    darwin_objc_rect(object,native_ui_selector("bounds"),ui.api.message,rect);
    trace_record(tag,(uintptr_t)object,rect[0],rect[1]);
    trace_record("UI_BOUNDS_SIZE width=0x",rect[2],rect[3],0);
}
static void native_ui_layout_description(void *window)
{
    /* Explicit runner evidence only. Read UIKit's own diagnostic view tree
     * on its normal main run loop; never invoke control actions or alter layout.
     * Limit text emitted by this runtime, and never call host stdio with guest TLS.
     */
    if (!ui.capture_path[0] || ui.sequence!=1) return;
    void *selector=native_ui_selector("recursiveDescription");
    if (!((signed char (*)(void *,void *,void *))(uintptr_t)ui.api.message)
            (window,native_ui_selector("respondsToSelector:"),selector)) return;
    void *description=((void *(*)(void *,void *))(uintptr_t)ui.api.message)(window,selector);
    const char *text=description ? native_ui_object(description,"UTF8String") : NULL;
    if (!text) return;
    unsigned long length=0; while (length<8192 && text[length]) length++;
    const char marker[]="UI_VIEW_TREE genuine UIKit recursiveDescription follows\n";
    RAW(SYS_write,native_trace_fd,marker,sizeof(marker)-1,0,0,0);
    RAW(SYS_write,native_trace_fd,text,length,0,0,0);
    RAW(SYS_write,native_trace_fd,"\n",1,0,0,0);
}
static void native_ui_capture(void *pixels,unsigned long bytes)
{
    if (!ui.capture_path[0]) return;
    /* Retain the last completed render, not only a possibly empty first frame. */
    long fd=RAW(SYS_openat,AT_FDCWD,ui.capture_path,O_WRONLY|O_CREAT|O_TRUNC|O_CLOEXEC,0600,0,0);
    if (fd<0) { trace_record("UI_FRAME_CAPTURE_ERROR fd=0x",fd,0,0); return; }
    uint32_t header[3]={ui.width,ui.height,ui.width*4};
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
