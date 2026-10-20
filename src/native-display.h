/* Native Wayland client in a host child, isolated from Darwin TLS. A blank
 * mapped surface is backend readiness, never evidence of UIKit rendering. */
#pragma once
#include <wayland-client.h>
#include "xdg-shell-client-protocol.h"
#include <sys/prctl.h>
#include <sys/wait.h>
#include <poll.h>
#include <signal.h>
#include <sys/eventfd.h>
#include "ios-platform-abi.h"
static int native_display_enabled;
static void *native_display_pixels;
struct native_wl_output { struct wl_output *object; uint32_t refresh; };
struct native_wl_buffer { struct wl_buffer *object; struct darwin_frame_control *control; unsigned int index; };
struct native_wayland {
    struct wl_display *display;
    struct wl_compositor *compositor;
    struct wl_shm *shm;
    struct xdg_wm_base *shell;
    struct wl_surface *surface;
    struct native_wl_buffer buffers[2];
    struct darwin_frame_control *control;
    struct native_wl_output outputs[8],*current_output;
    unsigned int output_count;
    uint32_t width,height;
    int ready_fd,pixels_fd,notify_fd,running,configured,ready,xrgb,focused;
};
static void native_window_publish(struct native_wayland *w,uint32_t flags)
{
    __atomic_fetch_add(&w->control->window_sequence,1,__ATOMIC_SEQ_CST);
    __atomic_store_n(&w->control->width,w->width,__ATOMIC_RELAXED);
    __atomic_store_n(&w->control->height,w->height,__ATOMIC_RELAXED);
    __atomic_store_n(&w->control->refresh_millihz,
        w->current_output ? w->current_output->refresh : 0,__ATOMIC_RELAXED);
    __atomic_store_n(&w->control->window_flags,flags,__ATOMIC_RELAXED);
    __atomic_fetch_add(&w->control->window_sequence,1,__ATOMIC_SEQ_CST);
}
static uint32_t native_window_flags(const struct native_wayland *w)
{
    return ARLINUX_IOS_WINDOW_BACKEND_ALIVE |
        (w->configured ? ARLINUX_IOS_WINDOW_CONFIGURED : 0) |
        (w->focused>0 ? ARLINUX_IOS_WINDOW_ACTIVE : 0);
}
static void native_wm_ping(void *data,struct xdg_wm_base *shell,uint32_t serial)
{ (void)data; xdg_wm_base_pong(shell,serial); }
static const struct xdg_wm_base_listener native_wm_listener={.ping=native_wm_ping};
static void native_shm_format(void *data,struct wl_shm *shm,uint32_t format)
{ (void)shm; if (format==WL_SHM_FORMAT_XRGB8888) ((struct native_wayland *)data)->xrgb=1; }
static const struct wl_shm_listener native_shm_listener={.format=native_shm_format};
static void native_output_geometry(void *data,struct wl_output *output,int32_t x,int32_t y,
                                    int32_t width,int32_t height,int32_t subpixel,
                                    const char *make,const char *model,int32_t transform)
{
    (void)data; (void)output; (void)x; (void)y; (void)width; (void)height;
    (void)subpixel; (void)make; (void)model; (void)transform;
}
static void native_output_mode(void *data,struct wl_output *output,uint32_t flags,
                               int32_t width,int32_t height,int32_t refresh)
{
    (void)output; (void)width; (void)height;
    if ((flags&WL_OUTPUT_MODE_CURRENT) && refresh>=0) ((struct native_wl_output *)data)->refresh=refresh;
}
static const struct wl_output_listener native_output_listener={
    .geometry=native_output_geometry,.mode=native_output_mode
};
static void native_surface_enter(void *data,struct wl_surface *surface,struct wl_output *output)
{
    struct native_wayland *w=data; (void)surface;
    if (!w->current_output) for (unsigned int i=0;i<w->output_count;i++)
        if (w->outputs[i].object==output) { w->current_output=w->outputs+i; break; }
}
static void native_surface_leave(void *data,struct wl_surface *surface,struct wl_output *output)
{
    struct native_wayland *w=data; (void)surface;
    if (w->current_output && w->current_output->object==output) w->current_output=NULL;
}
static const struct wl_surface_listener native_wl_surface_listener={
    .enter=native_surface_enter,.leave=native_surface_leave
};
static void native_wl_global(void *data,struct wl_registry *registry,uint32_t name,
                             const char *interface,uint32_t version)
{
    struct native_wayland *w=data; (void)version;
    if (!strcmp(interface,"wl_compositor")) w->compositor=wl_registry_bind(registry,name,&wl_compositor_interface,1);
    else if (!strcmp(interface,"wl_shm")) {
        w->shm=wl_registry_bind(registry,name,&wl_shm_interface,1);
        wl_shm_add_listener(w->shm,&native_shm_listener,w);
    }
    else if (!strcmp(interface,"xdg_wm_base")) {
        w->shell=wl_registry_bind(registry,name,&xdg_wm_base_interface,1);
        xdg_wm_base_add_listener(w->shell,&native_wm_listener,w);
    }
    else if (!strcmp(interface,"wl_output") && w->output_count<8) {
        struct native_wl_output *output=w->outputs+w->output_count++;
        output->object=wl_registry_bind(registry,name,&wl_output_interface,1);
        wl_output_add_listener(output->object,&native_output_listener,output);
    }
}
static void native_wl_remove(void *data,struct wl_registry *registry,uint32_t name)
{ (void)data; (void)registry; (void)name; }
static const struct wl_registry_listener native_registry_listener={native_wl_global,native_wl_remove};
static void native_wl_frame(void *data,struct wl_callback *callback,uint32_t time)
{
    struct native_wayland *w=data; (void)time; wl_callback_destroy(callback);
    if (!w->ready) {
        native_window_publish(w,native_window_flags(w));
        uint32_t geometry[3]={w->width,w->height,w->current_output ? w->current_output->refresh : 0};
        need(write(w->ready_fd,geometry,sizeof(geometry))==(ssize_t)sizeof(geometry),"Wayland readiness reply");
        close(w->ready_fd); w->ready=1;
        fprintf(stderr,"WAYLAND_BACKEND_READY width=%u height=%u refresh_millihz=%u app_frame_presented=false\n",w->width,w->height,geometry[2]);
    }
}
static const struct wl_callback_listener native_frame_listener={.done=native_wl_frame};
static void native_buffer_release(void *data,struct wl_buffer *buffer)
{
    struct native_wl_buffer *entry=data; (void)buffer;
    __atomic_store_n(entry->control->state+entry->index,D_FRAME_FREE,__ATOMIC_RELEASE);
}
static const struct wl_buffer_listener native_buffer_listener={.release=native_buffer_release};
static void native_surface_configure(void *data,struct xdg_surface *surface,uint32_t serial)
{
    struct native_wayland *w=data;
    xdg_surface_ack_configure(surface,serial);
    if (w->configured) return;
    need(w->width && w->height && w->width<=8192 && w->height<=8192,"Wayland configured geometry");
    int bytes=w->width*w->height*4;
    need(!ftruncate(w->pixels_fd,bytes*2),"Wayland shared framebuffer allocation");
    struct wl_shm_pool *pool=wl_shm_create_pool(w->shm,w->pixels_fd,bytes*2);
    for (unsigned int i=0;i<2;i++) {
        struct native_wl_buffer *entry=w->buffers+i;
        entry->control=w->control; entry->index=i;
        entry->object=wl_shm_pool_create_buffer(pool,i*bytes,w->width,w->height,w->width*4,WL_SHM_FORMAT_XRGB8888);
        need(entry->object!=NULL,"Wayland frame buffer");
        wl_buffer_add_listener(entry->object,&native_buffer_listener,entry);
    }
    wl_shm_pool_destroy(pool);
    struct wl_callback *frame=wl_surface_frame(w->surface);
    wl_callback_add_listener(frame,&native_frame_listener,w);
    __atomic_store_n(w->control->state,D_FRAME_BUSY,__ATOMIC_RELEASE);
    wl_surface_attach(w->surface,w->buffers[0].object,0,0);
    wl_surface_damage(w->surface,0,0,w->width,w->height);
    wl_surface_commit(w->surface); w->configured=1;
}
static const struct xdg_surface_listener native_surface_listener={.configure=native_surface_configure};
static void native_toplevel_configure(void *data,struct xdg_toplevel *top,int32_t width,
                                      int32_t height,struct wl_array *states)
{
    struct native_wayland *w=data; (void)top;
    int focused=0; uint32_t *state;
    wl_array_for_each(state,states)
        if (*state==XDG_TOPLEVEL_STATE_ACTIVATED) focused=1;
    if (w->focused!=focused) {
        w->focused=focused;
        fprintf(stderr,"WAYLAND_TOPLEVEL_FOCUS backend=%ld app=%ld activated=%d\n",
                (long)getpid(),(long)getppid(),focused);
    }
    if (!w->configured) {
        if (width>0) w->width=width;
        if (height>0) w->height=height;
    }
    native_window_publish(w,native_window_flags(w));
}
static void native_toplevel_close(void *data,struct xdg_toplevel *top)
{
    struct native_wayland *w=data; (void)top;
    native_window_publish(w,native_window_flags(w)|ARLINUX_IOS_WINDOW_CLOSE_REQUESTED);
    w->running=0;
}
static const struct xdg_toplevel_listener native_toplevel_listener={
    .configure=native_toplevel_configure,.close=native_toplevel_close
};
struct native_app_frame { uint32_t sequence; };
static void native_app_frame_callback(void *data,struct wl_callback *callback,uint32_t time)
{
    struct native_app_frame *frame=data;
    fprintf(stderr,"WAYLAND_APP_FRAME_CALLBACK backend=%ld sequence=%u time=%u\n",(long)getpid(),frame->sequence,time);
    wl_callback_destroy(callback); free(frame);
}
static const struct wl_callback_listener native_app_frame_listener={.done=native_app_frame_callback};
static void native_submit_frames(struct native_wayland *w)
{
    for (unsigned int i=0;i<2;i++) {
        uint32_t ready=D_FRAME_READY;
        if (!__atomic_compare_exchange_n(w->control->state+i,&ready,D_FRAME_BUSY,0,__ATOMIC_ACQ_REL,__ATOMIC_RELAXED)) continue;
        struct native_app_frame *frame=calloc(1,sizeof(*frame));
        need(frame!=NULL,"Wayland frame callback storage");
        frame->sequence=w->control->sequence[i];
        wl_callback_add_listener(wl_surface_frame(w->surface),&native_app_frame_listener,frame);
        wl_surface_attach(w->surface,w->buffers[i].object,0,0);
        wl_surface_damage(w->surface,0,0,w->width,w->height);
        wl_surface_commit(w->surface);
        fprintf(stderr,"WAYLAND_APP_FRAME_COMMITTED backend=%ld app=%ld sequence=%u slot=%u\n",
                (long)getpid(),(long)getppid(),frame->sequence,i);
    }
}
static void native_wayland_run(int ready_fd,int pixels_fd,int notify,struct darwin_frame_control *control,pid_t parent)
{
    need(!prctl(PR_SET_PDEATHSIG,SIGTERM) && getppid()==parent,"Wayland backend parent lifetime");
    struct native_wayland w={.ready_fd=ready_fd,.pixels_fd=pixels_fd,.notify_fd=notify,.control=control,
        .running=1,.width=320,.height=480,.focused=-1};
    w.display=wl_display_connect(NULL);
    need(w.display!=NULL,"connect native Wayland compositor");
    struct wl_registry *registry=wl_display_get_registry(w.display);
    wl_registry_add_listener(registry,&native_registry_listener,&w);
    need(wl_display_roundtrip(w.display)>=0 && w.compositor && w.shm && w.shell,"Wayland display interfaces");
    need(wl_display_roundtrip(w.display)>=0 && w.xrgb,"Wayland XRGB framebuffer capability");
    w.surface=wl_compositor_create_surface(w.compositor);
    wl_surface_add_listener(w.surface,&native_wl_surface_listener,&w);
    struct xdg_surface *surface=xdg_wm_base_get_xdg_surface(w.shell,w.surface);
    xdg_surface_add_listener(surface,&native_surface_listener,&w);
    struct xdg_toplevel *top=xdg_surface_get_toplevel(surface);
    xdg_toplevel_add_listener(top,&native_toplevel_listener,&w);
    xdg_toplevel_set_title(top,"ARLinux Wayland backend - no app frame yet");
    xdg_toplevel_set_app_id(top,"io.arlinux.darwin-display");
    const char *mode=getenv("ARLINUX_WINDOW_MODE");
    if (mode && !strcmp(mode,"fullscreen")) xdg_toplevel_set_fullscreen(top,NULL);
    wl_surface_commit(w.surface);
    while (w.running) {
        if (wl_display_dispatch_pending(w.display)<0) break;
        int flushed=wl_display_flush(w.display);
        if (flushed<0 && errno!=EAGAIN) break;
        struct pollfd fds[2]={{.fd=wl_display_get_fd(w.display),.events=POLLIN|(flushed<0 ? POLLOUT : 0)},
                              {.fd=notify,.events=POLLIN}};
        int result=poll(fds,2,-1);
        if (result<0 && errno==EINTR) continue;
        if (result<0 || (fds[0].revents&(POLLHUP|POLLERR))) break;
        if ((fds[0].revents&POLLIN) && wl_display_dispatch(w.display)<0) break;
        if (fds[1].revents&POLLIN) {
            uint64_t token; need(read(notify,&token,sizeof(token))==(ssize_t)sizeof(token),"Wayland frame notification");
            native_submit_frames(&w);
        }
    }
    uint32_t final_flags=__atomic_load_n(&w.control->window_flags,__ATOMIC_RELAXED);
    native_window_publish(&w,final_flags&~ARLINUX_IOS_WINDOW_BACKEND_ALIVE);
    wl_display_disconnect(w.display); _exit(0);
}
static void native_display_open(void)
{
    if (!getenv("WAYLAND_DISPLAY") && !getenv("WAYLAND_SOCKET")) return;
    int ready[2]; need(!pipe2(ready,O_CLOEXEC),"Wayland backend readiness pipe");
    int pixels=memfd_create("arlinux-ios-framebuffer",MFD_CLOEXEC);
    need(pixels>=0,"Wayland framebuffer memfd");
    int notify=eventfd(0,EFD_CLOEXEC|EFD_NONBLOCK); need(notify>=0,"Wayland frame eventfd");
    struct darwin_frame_control *control=mmap(NULL,sizeof(*control),PROT_READ|PROT_WRITE,MAP_SHARED|MAP_ANONYMOUS,-1,0);
    need(control!=MAP_FAILED,"shared frame ownership");
    pid_t parent=getpid(),child=fork(); need(child>=0,"Wayland backend process");
    if (!child) { close(ready[0]); native_wayland_run(ready[1],pixels,notify,control,parent); }
    close(ready[1]);
    struct pollfd descriptor={.fd=ready[0],.events=POLLIN};
    uint32_t geometry[3];
    int received=poll(&descriptor,1,5000)>0 && read(ready[0],geometry,sizeof(geometry))==(ssize_t)sizeof(geometry);
    close(ready[0]);
    if (!received) { kill(child,SIGTERM); waitpid(child,NULL,0); need(0,"Wayland backend did not configure a surface"); }
    native_display_pixels=mmap(NULL,(size_t)geometry[0]*geometry[1]*8,PROT_READ|PROT_WRITE,MAP_SHARED,pixels,0);
    need(native_display_pixels!=MAP_FAILED,"map real Wayland framebuffer"); close(pixels);
    double period=geometry[2] ? 1000.0/(double)geometry[2] : 0; uint64_t period_bits;
    memcpy(&period_bits,&period,sizeof(period_bits));
    need(!darwin_native_display_register(geometry[0],geometry[1],geometry[2],period_bits,child),"register native Wayland display endpoint");
    darwin_native_ui_output(native_display_pixels,geometry[0],geometry[1],control,notify);
    native_display_enabled=1;
}
