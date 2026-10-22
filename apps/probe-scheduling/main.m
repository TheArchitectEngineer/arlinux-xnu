#import "../shared/Screen.h"
#include <sys/socket.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>

/* Public iOS APIs only. No workqueue syscalls, runtime internals or harness
 * callbacks: the external pointer starts work and observes the rendered result. */
@interface SchedulingProbe : UIResponder <UIApplicationDelegate>
@property(nonatomic,strong) UIWindow *window;
@property(nonatomic,strong) UILabel *result;
@property(nonatomic) BOOL busy;
@property(nonatomic) NSUInteger generation;
@end
@implementation SchedulingProbe
- (BOOL)application:(UIApplication *)app didFinishLaunchingWithOptions:(NSDictionary *)options {
    (void)app; (void)options;
    UIView *view; self.window=Screen(@"Scheduling probe",&view);
    self.result=Label(view,@"Ready",85);
    Button(view,@"Blocking",self,@selector(blocking:),145);
    Button(view,@"Backlog",self,@selector(backlog:),205);
    Button(view,@"Timer IO",self,@selector(timerIO:),265);
    [self.window makeKeyAndVisible]; AppInspectionStart(self.window); return YES;
}
- (NSUInteger)begin:(NSString *)name {
    if (self.busy) return 0;
    self.busy=YES; NSUInteger generation=++self.generation;
    self.result.text=[name stringByAppendingString:@" running"];
    printf("APP_SCHEDULING case=%s generation=%lu stage=submit\n",name.UTF8String,(unsigned long)generation); fflush(stdout);
    return generation;
}
- (void)finish:(NSString *)name generation:(NSUInteger)generation ok:(BOOL)ok {
    NSAssert(NSThread.isMainThread,@"Result must be delivered on the main thread");
    if (generation!=self.generation || !self.busy) return;
    self.busy=NO;
    self.result.text=[NSString stringWithFormat:@"%@: %@ %lu",name,ok ? @"OK" : @"FAILED",(unsigned long)generation];
    printf("APP_SCHEDULING case=%s generation=%lu stage=main ok=%d\n",name.UTF8String,(unsigned long)generation,ok); fflush(stdout);
}
- (void)blocking:(id)sender {
    (void)sender; NSUInteger generation=[self begin:@"Blocking"]; if (!generation) return;
    dispatch_group_t group=dispatch_group_create();
    dispatch_semaphore_t gate=dispatch_semaphore_create(0);
    __block unsigned started=0,finished=0,errors=0;
    for (unsigned i=0;i<2;i++) {
        dispatch_group_async(group,dispatch_get_global_queue(QOS_CLASS_DEFAULT,0),^{
            dispatch_async(dispatch_get_main_queue(),^{
                if (++started==2) {
                    /* Both submitted tasks have entered. Their releaser needs
                     * another worker, even on the actual single CPU. */
                    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED,0),^{
                        puts("APP_SCHEDULING case=Blocking stage=release"); fflush(stdout);
                        dispatch_semaphore_signal(gate); dispatch_semaphore_signal(gate);
                    });
                }
            });
            printf("APP_SCHEDULING case=Blocking worker=%u stage=wait\n",i); fflush(stdout);
            long error=dispatch_semaphore_wait(gate,dispatch_time(DISPATCH_TIME_NOW,5*NSEC_PER_SEC));
            printf("APP_SCHEDULING case=Blocking worker=%u stage=returned result=%ld\n",i,error); fflush(stdout);
            dispatch_async(dispatch_get_main_queue(),^{finished++; if (error) errors++;});
        });
    }
    dispatch_group_notify(group,dispatch_get_main_queue(),^{
        [self finish:@"Blocking" generation:generation ok:started==2 && finished==2 && !errors];
    });
}
- (void)backlog:(id)sender {
    (void)sender; NSUInteger generation=[self begin:@"Backlog"]; if (!generation) return;
    dispatch_group_t group=dispatch_group_create();
    __block uint64_t seen=0; __block unsigned delivered=0;
    for (unsigned i=0;i<64;i++) {
        dispatch_group_async(group,dispatch_get_global_queue(QOS_CLASS_DEFAULT,0),^{
            /* Bounded CPU work gives libdispatch opportunities to relinquish
             * its worker. Acceptance checks every delivery, not elapsed time. */
            volatile uint64_t value=i+1;
            for (unsigned j=0;j<200000;j++) value=value*6364136223846793005ULL+1;
            (void)value;
            dispatch_async(dispatch_get_main_queue(),^{seen|=UINT64_C(1)<<i; delivered++;});
        });
    }
    dispatch_group_notify(group,dispatch_get_main_queue(),^{
        [self finish:@"Backlog" generation:generation ok:seen==UINT64_MAX && delivered==64];
    });
}
- (void)timerIO:(id)sender {
    (void)sender; NSUInteger generation=[self begin:@"Timer IO"]; if (!generation) return;
    int descriptors[2];
    if (socketpair(AF_UNIX,SOCK_STREAM,0,descriptors)) {
        printf("APP_SCHEDULING case=TimerIO stage=socketpair errno=%d\n",errno); fflush(stdout);
        [self finish:@"Timer IO" generation:generation ok:NO]; return;
    }
    int reader=descriptors[0],writer=descriptors[1];
    dispatch_queue_t queue=dispatch_queue_create("io.arlinux.probe.events",DISPATCH_QUEUE_SERIAL);
    dispatch_source_t input=dispatch_source_create(DISPATCH_SOURCE_TYPE_READ,(uintptr_t)reader,0,queue);
    dispatch_source_t timer=dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER,0,0,queue);
    if (!input || !timer) {
        if (input) {
            dispatch_source_set_cancel_handler(input,^{close(reader);});
            dispatch_source_cancel(input); dispatch_resume(input);
        } else close(reader);
        if (timer) {
            dispatch_source_set_cancel_handler(timer,^{close(writer);});
            dispatch_source_cancel(timer); dispatch_resume(timer);
        } else close(writer);
        [self finish:@"Timer IO" generation:generation ok:NO]; return;
    }
    __block unsigned ticks=0,received=0; __block BOOL failed=NO;
    dispatch_source_set_cancel_handler(input,^{close(reader);});
    dispatch_source_set_cancel_handler(timer,^{close(writer);});
    dispatch_source_set_event_handler(timer,^{
        unsigned char byte=(unsigned char)(++ticks);
        if (write(writer,&byte,1)!=1) failed=YES;
        printf("APP_SCHEDULING case=TimerIO stage=timer tick=%u failed=%d\n",ticks,failed); fflush(stdout);
        if (failed || ticks==3) dispatch_source_cancel(timer);
    });
    dispatch_source_set_event_handler(input,^{
        unsigned char bytes[3]; ssize_t count=read(reader,bytes,sizeof(bytes));
        if (count<=0) failed=YES;
        for (ssize_t i=0;i<count;i++) if (bytes[i]!=++received) failed=YES;
        printf("APP_SCHEDULING case=TimerIO stage=read bytes=%ld received=%u failed=%d\n",(long)count,received,failed); fflush(stdout);
        if (failed || received==3) {
            dispatch_source_cancel(input); dispatch_source_cancel(timer);
            BOOL ok=!failed && received==3 && ticks==3;
            dispatch_async(dispatch_get_main_queue(),^{[self finish:@"Timer IO" generation:generation ok:ok];});
        }
    });
    dispatch_source_set_timer(timer,dispatch_time(DISPATCH_TIME_NOW,100*NSEC_PER_MSEC),100*NSEC_PER_MSEC,0);
    dispatch_resume(input); dispatch_resume(timer);
    /* A bounded failed observation releases sources; it supplies no bytes,
     * timer events or success callback. The external journey also has a limit. */
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW,5*NSEC_PER_SEC),queue,^{
        if (received!=3) {
            dispatch_source_cancel(input); dispatch_source_cancel(timer);
            dispatch_async(dispatch_get_main_queue(),^{[self finish:@"Timer IO" generation:generation ok:NO];});
        }
    });
}
@end
int main(int argc,char **argv) {
    @autoreleasepool { return UIApplicationMain(argc,argv,nil,NSStringFromClass(SchedulingProbe.class)); }
}
