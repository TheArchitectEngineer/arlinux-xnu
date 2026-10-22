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
    if (!ui.inspect_tree || ui.sequence!=1) return;
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
