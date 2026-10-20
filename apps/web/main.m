#import <UIKit/UIKit.h>
#import <WebKit/WebKit.h>
@interface WebDelegate : UIResponder <UIApplicationDelegate>
@property(nonatomic,strong) UIWindow *window; @property(nonatomic,strong) WKWebView *web;
@end
@implementation WebDelegate
- (BOOL)application:(UIApplication *)app didFinishLaunchingWithOptions:(NSDictionary *)options {
    (void)app; (void)options; self.window=[[UIWindow alloc] initWithFrame:UIScreen.mainScreen.bounds];
    UIViewController *controller=[UIViewController new]; self.window.rootViewController=controller;
    self.web=[[WKWebView alloc] initWithFrame:controller.view.bounds]; self.web.autoresizingMask=UIViewAutoresizingFlexibleWidth|UIViewAutoresizingFlexibleHeight;
    [controller.view addSubview:self.web];
    [self.web loadHTMLString:@"<meta name='viewport' content='width=device-width,initial-scale=1'><style>body{font:24px sans-serif;background:white;color:black}button{font-size:24px;margin-top:30px}</style><h1>Web</h1><p id='count'>WebCount: 0</p><button onclick=\"document.getElementById('count').textContent='WebCount: 1'\">Advance</button>" baseURL:nil];
    [self.window makeKeyAndVisible]; return YES;
}
@end
int main(int argc,char **argv) { @autoreleasepool { return UIApplicationMain(argc,argv,nil,NSStringFromClass(WebDelegate.class)); } }
