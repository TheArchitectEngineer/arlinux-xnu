#import "../shared/Screen.h"
#include <pthread.h>
#include <sys/mman.h>
#include <stdio.h>
static void *compute(void *value) { *(int *)value=42; return (void *)37; }
@interface WorkersDelegate : UIResponder <UIApplicationDelegate>
@property(nonatomic,strong) UIWindow *window; @property(nonatomic,strong) UILabel *result;
@property(nonatomic) NSUInteger queueValue;
@end
@implementation WorkersDelegate
- (BOOL)application:(UIApplication *)app didFinishLaunchingWithOptions:(NSDictionary *)options {
    (void)app; (void)options; UIView *view; self.window=Screen(@"Workers",&view); self.result=Label(view,@"Ready",85);
    Button(view,@"Thread",self,@selector(thread:),145); Button(view,@"Memory",self,@selector(memory:),205);
    Button(view,@"Queue",self,@selector(queue:),265); [self.window makeKeyAndVisible]; AppInspectionStart(self.window); return YES;
}
- (void)thread:(id)sender {
    (void)sender; int value=0; void *reply=0; pthread_t thread;
    int error=pthread_create(&thread,0,compute,&value); if (!error) error=pthread_join(thread,&reply);
    self.result.text=!error && value==42 && reply==(void *)37 ? @"Thread: 42" : @"Thread failed";
}
- (void)memory:(id)sender {
    (void)sender; volatile unsigned char *bytes=mmap(0,16384,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON,-1,0);
    BOOL ok=bytes!=MAP_FAILED;
    if (ok) {
        for (unsigned i=0;i<16384;i++) bytes[i]=(unsigned char)(i^0x5a);
        for (unsigned i=0;i<16384;i++) if (bytes[i]!=(unsigned char)(i^0x5a)) ok=NO;
        if (munmap((void *)bytes,16384)) ok=NO;
    }
    self.result.text=ok ? @"Memory: 16384" : @"Memory failed";
}
- (void)queue:(id)sender {
    (void)sender;
    puts("APP_WORKER_QUEUE stage=submit");fflush(stdout);
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_DEFAULT,0),^{
        NSUInteger value=[@[@2,@3,@5] count];
        dispatch_queue_t main=dispatch_get_main_queue();
        printf("APP_WORKER_QUEUE stage=background value=%lu main_queue=%p\n",(unsigned long)value,(__bridge void *)main);fflush(stdout);
        dispatch_async(main,^{
            puts("APP_WORKER_QUEUE stage=main");fflush(stdout);
            self.queueValue+=value;
            self.result.text=value==3 ? [NSString stringWithFormat:@"Queue: %lu",(unsigned long)self.queueValue] : @"Queue failed";
        });
    });
}
@end
int main(int argc,char **argv) { @autoreleasepool { return UIApplicationMain(argc,argv,nil,NSStringFromClass(WorkersDelegate.class)); } }
