#import "../shared/Screen.h"
#import "PrivateXPC.h"
@interface ServicesDelegate : UIResponder <UIApplicationDelegate>
@property(strong) UIWindow *window; @property(strong) UILabel *result; @property(strong) NSXPCConnection *connection;
@end
@implementation ServicesDelegate
- (BOOL)application:(UIApplication *)app didFinishLaunchingWithOptions:(NSDictionary *)options {
    (void)app; (void)options; UIView *view; self.window=Screen(@"Services",&view); self.result=Label(view,@"Ready",85);
    Button(view,@"Send",self,@selector(send:),160); [self.window makeKeyAndVisible]; return YES;
}
- (void)send:(id)sender {
    (void)sender; self.connection=[[NSXPCConnection alloc] initWithServiceName:@"io.arlinux.EchoWorker"];
    self.connection.remoteObjectInterface=[NSXPCInterface interfaceWithProtocol:@protocol(EchoProtocol)]; [self.connection resume];
    id<EchoProtocol> remote=[self.connection remoteObjectProxyWithErrorHandler:^(NSError *error) {
        (void)error; dispatch_async(dispatch_get_main_queue(),^{ self.result.text=@"XPC failed"; });
    }];
    [remote echo:@"payload-42" reply:^(NSString *reply) {
        dispatch_async(dispatch_get_main_queue(),^{ self.result.text=[@"Echo: " stringByAppendingString:reply]; });
    }];
}
@end
int main(int argc,char **argv) { @autoreleasepool { return UIApplicationMain(argc,argv,nil,NSStringFromClass(ServicesDelegate.class)); } }
