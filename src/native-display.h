/* Host-only setup before Darwin takes over TLS. X11 owns the actual window;
 * no host-libc/Xlib call is made from a guest callback. Presentation and input
 * have not been connected to UIKit yet. An empty backend window is not an app
 * screenshot or proof that the application displayed anything. */
#pragma once
#include <X11/Xlib.h>
static Display *native_x_display;
static int native_display_enabled;
static void native_display_open(void)
{
    if (!getenv("DISPLAY")) return;
    native_x_display=XOpenDisplay(NULL);
    need(native_x_display!=NULL,"connect actual Linux X11 display");
    int screen=DefaultScreen(native_x_display);
    Window window=XCreateSimpleWindow(native_x_display,RootWindow(native_x_display,screen),
                                       0,0,320,480,0,0,0);
    need(window!=0,"create Linux display window");
    XStoreName(native_x_display,window,"ARLinux display backend - no app frame yet");
    XMapWindow(native_x_display,window);
    XSync(native_x_display,False);
    XWindowAttributes actual;
    need(XGetWindowAttributes(native_x_display,window,&actual)!=0 && actual.width>0 && actual.height>0,
         "read actual Linux window geometry");
    need(!darwin_native_display_register(actual.width,actual.height),"register actual Linux display endpoint");
    native_display_enabled=1;
    fprintf(stderr,"LINUX_DISPLAY_BACKEND window=%#lx width=%d height=%d app_frame_presented=false\n",
            window,actual.width,actual.height);
}
