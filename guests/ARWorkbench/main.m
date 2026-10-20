/* ARWorkbench 1.0: real UIKit actions, real POSIX effects; no driver RPC. */
#import "../sdk/XPC.h"
typedef unsigned long pthread_t;
extern int pthread_create(pthread_t *,const void *,void *(*)(void *),void *);
extern int pthread_join(pthread_t,void **);
extern void *mmap(void *,unsigned long,int,int,int,long);
extern int munmap(void *,unsigned long);
extern int fork(void),waitpid(int,int *,int);
extern void _exit(int);
extern int open(const char *,int,...),close(int);
extern long read(int,void *,unsigned long);
enum { ReadOnly=0, WriteOnly=1, Create=0x200, Truncate=0x400,
       ProtReadWrite=3, MapPrivate=2, MapAnon=0x1000,
       ButtonTypeSystem=1, TouchUpInside=1UL<<6, GuestPage=16384 };
@interface ARWorkbenchDelegate : UIResponder {
    UIWindow *_window;
    UILabel *_result;
    UITextField *_input;
    NSXPCConnection *_connection;
}
- (BOOL)application:(UIApplication *)application didFinishLaunchingWithOptions:(id)options;
- (void)save:(id)sender;
- (void)thread:(id)sender;
- (void)memory:(id)sender;
- (void)helper:(id)sender;
- (void)xpc:(id)sender;
@end
static void *worker(void *pointer) { *(volatile int *)pointer=42; return (void *)37; }
@implementation ARWorkbenchDelegate
- (BOOL)application:(UIApplication *)application didFinishLaunchingWithOptions:(id)options
{
    (void)application; (void)options;
    _window=[[UIWindow alloc] initWithFrame:[[UIScreen mainScreen] bounds]];
    UIViewController *controller=[[UIViewController alloc] init];
    UIView *view=[controller view]; [view setBackgroundColor:[UIColor whiteColor]];
    UILabel *title=[[UILabel alloc] initWithFrame:(CGRect){{20,30},{280,35}}];
    [title setText:@"ARWorkbench 1.0"]; [title setTextColor:[UIColor blackColor]]; [view addSubview:title];
    _result=[[UILabel alloc] initWithFrame:(CGRect){{20,80},{280,45}}];
    [_result setText:@"Ready"]; [_result setTextColor:[UIColor blackColor]]; [view addSubview:_result];
    _input=[[UITextField alloc] initWithFrame:(CGRect){{20,140},{280,40}}];
    [_input setPlaceholder:@"Edit"]; [view addSubview:_input];
    NSString *names[]={@"Save",@"Thread",@"Memory",@"Helper",@"XPC"};
    SEL actions[]={@selector(save:),@selector(thread:),@selector(memory:),@selector(helper:),@selector(xpc:)};
    for (unsigned int i=0;i<5;i++) {
        UIButton *button=[UIButton buttonWithType:ButtonTypeSystem];
        [button setFrame:(CGRect){{20+(i%2)*150,200+(i/2)*50},{130,40}}]; [button setTitle:names[i] forState:0];
        [button addTarget:self action:actions[i] forControlEvents:TouchUpInside]; [view addSubview:button];
    }
    char note[128]; int fd=open("note.txt",ReadOnly);
    if (fd>=0) {
        long count=read(fd,note,sizeof(note)-1); close(fd);
        if (count>=0) { note[count]=0; [_result setText:[NSString stringWithFormat:@"Saved: %@",[NSString stringWithUTF8String:note]]]; }
    }
    [_window setRootViewController:controller]; [_window makeKeyAndVisible];
    const char ready[]="ARWORKBENCH_READY version=1.0\n"; write(1,ready,sizeof(ready)-1); return 1;
}
- (void)save:(id)sender
{
    (void)sender; [_input resignFirstResponder];
    NSString *value=[_input text]; const char *text=[value UTF8String];
    unsigned long count=0; if (text) while (text[count]) count++;
    int fd=open("note.txt",WriteOnly|Create|Truncate,0600);
    long result=fd>=0 ? write(fd,text,count) : -1; if (fd>=0) close(fd);
    [_result setText:result==(long)count ? [NSString stringWithFormat:@"Saved: %@",value] : @"Save failed"];
}
- (void)thread:(id)sender
{
    (void)sender; int shared=0; void *result=0; pthread_t thread;
    int status=pthread_create(&thread,0,worker,&shared);
    if (!status) status=pthread_join(thread,&result);
    [_result setText:!status && result==(void *)37 && shared==42 ? @"Thread: 42" : @"Thread failed"];
}
- (void)memory:(id)sender
{
    (void)sender;
    volatile unsigned char *bytes=mmap(0,GuestPage,ProtReadWrite,MapPrivate|MapAnon,-1,0);
    int good=bytes!=(void *)-1;
    if (good) {
        for (unsigned int i=0;i<GuestPage;i++) bytes[i]=(unsigned char)(i^0x5a);
        for (unsigned int i=0;i<GuestPage;i++) if (bytes[i]!=(unsigned char)(i^0x5a)) good=0;
        if (munmap((void *)bytes,GuestPage)) good=0;
    }
    [_result setText:good ? @"Memory: 16384" : @"Memory failed"];
}
- (void)helper:(id)sender
{
    (void)sender; int status=0; int child=fork(); if (!child) _exit(37);
    int waited=child>0 ? waitpid(child,&status,0) : -1;
    [_result setText:waited==child && child>0 && status==(37<<8) ? @"Helper: 37" : @"Helper failed"];
}
- (void)xpc:(id)sender
{
    (void)sender;
    _connection=[[NSXPCConnection alloc] initWithServiceName:@"io.arlinux.ARJourneyWorker"];
    [_connection setRemoteObjectInterface:[NSXPCInterface interfaceWithProtocol:@protocol(ARJourneyWorker)]];
    [_connection resume];
    id<ARJourneyWorker> proxy=[_connection remoteObjectProxyWithErrorHandler:^(NSError *error) {
        (void)error; dispatch_async(dispatch_get_main_queue(),^{ [_result setText:@"XPC failed"]; });
    }];
    [proxy ping:@"ping" reply:^(NSString *value) {
        dispatch_async(dispatch_get_main_queue(),^{ [_result setText:[NSString stringWithFormat:@"XPC: %@",value]]; });
    }];
}
@end
int main(int argc,char **argv)
{
    @autoreleasepool { return UIApplicationMain(argc,argv,nil,@"ARWorkbenchDelegate"); }
}
