#import "../shared/Screen.h"
#include <stdio.h>
@interface NetworkDelegate : UIResponder <UIApplicationDelegate>
@property(nonatomic,strong) UIWindow *window; @property(nonatomic,strong) UILabel *result;
@end
@implementation NetworkDelegate
- (BOOL)application:(UIApplication *)app didFinishLaunchingWithOptions:(NSDictionary *)options {
    (void)app; (void)options; UIView *view; self.window=Screen(@"Network",&view); self.result=Label(view,@"Offline",85);
    Button(view,@"Fetch",self,@selector(fetch:),160); [self.window makeKeyAndVisible]; AppInspectionStart(self.window); return YES;
}
- (void)fetch:(id)sender {
    (void)sender;
    fprintf(stderr,"APP_NETWORK stage=submit\n");
    NSURL *url=[NSURL URLWithString:@"http://127.0.0.1:18765/value.json"];
    NSURLSessionDataTask *task=[[NSURLSession sharedSession] dataTaskWithURL:url completionHandler:^(NSData *data,NSURLResponse *response,NSError *error) {
        fprintf(stderr,"APP_NETWORK stage=completion bytes=%lu status=%ld error_domain=%s error_code=%ld\n",
            (unsigned long)data.length,(long)[(NSHTTPURLResponse *)response statusCode],
            error ? error.domain.UTF8String : "none",(long)error.code);
        NSDictionary *body=data ? [NSJSONSerialization JSONObjectWithData:data options:0 error:nil] : nil;
        BOOL ok=!error && [(NSHTTPURLResponse *)response statusCode]==200 && [body[@"answer"] isEqual:@42];
        dispatch_async(dispatch_get_main_queue(),^{
            fprintf(stderr,"APP_NETWORK stage=main accepted=%d\n",ok);
            self.result.text=ok ? @"Network: 42" : @"Network failed";
        });
    }];
    fprintf(stderr,"APP_NETWORK stage=created task=%s\n",task ? "present" : "absent");
    [task resume];
    fprintf(stderr,"APP_NETWORK stage=resumed\n");
}
@end
int main(int argc,char **argv) { @autoreleasepool { return UIApplicationMain(argc,argv,nil,NSStringFromClass(NetworkDelegate.class)); } }
