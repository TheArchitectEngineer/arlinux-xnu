#import "../shared/Screen.h"
@interface NetworkDelegate : UIResponder <UIApplicationDelegate>
@property(nonatomic,strong) UIWindow *window; @property(nonatomic,strong) UILabel *result;
@end
@implementation NetworkDelegate
- (BOOL)application:(UIApplication *)app didFinishLaunchingWithOptions:(NSDictionary *)options {
    (void)app; (void)options; UIView *view; self.window=Screen(@"Network",&view); self.result=Label(view,@"Offline",85);
    Button(view,@"Fetch",self,@selector(fetch:),160); [self.window makeKeyAndVisible]; return YES;
}
- (void)fetch:(id)sender {
    (void)sender;
    NSURL *url=[NSURL URLWithString:@"http://127.0.0.1:18765/value.json"];
    [[[NSURLSession sharedSession] dataTaskWithURL:url completionHandler:^(NSData *data,NSURLResponse *response,NSError *error) {
        NSDictionary *body=data ? [NSJSONSerialization JSONObjectWithData:data options:0 error:nil] : nil;
        BOOL ok=!error && [(NSHTTPURLResponse *)response statusCode]==200 && [body[@"answer"] isEqual:@42];
        dispatch_async(dispatch_get_main_queue(),^{ self.result.text=ok ? @"Network: 42" : @"Network failed"; });
    }] resume];
}
@end
int main(int argc,char **argv) { @autoreleasepool { return UIApplicationMain(argc,argv,nil,NSStringFromClass(NetworkDelegate.class)); } }
