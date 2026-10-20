#import <UIKit/UIKit.h>
#include <stdio.h>
#import <WebKit/WebKit.h>
@interface WebDelegate : UIResponder <UIApplicationDelegate,WKScriptMessageHandler>
@property(nonatomic,strong) UIWindow *window; @property(nonatomic,strong) WKWebView *web;
@end
@implementation WebDelegate
- (BOOL)application:(UIApplication *)app didFinishLaunchingWithOptions:(NSDictionary *)options {
    (void)app; (void)options; self.window=[[UIWindow alloc] initWithFrame:UIScreen.mainScreen.bounds];
    UIViewController *controller=[UIViewController new]; self.window.rootViewController=controller;
    WKWebViewConfiguration *configuration=[WKWebViewConfiguration new];
    [configuration.userContentController addScriptMessageHandler:self name:@"effect"];
    self.web=[[WKWebView alloc] initWithFrame:controller.view.bounds configuration:configuration]; self.web.autoresizingMask=UIViewAutoresizingFlexibleWidth|UIViewAutoresizingFlexibleHeight;
    [controller.view addSubview:self.web];
    [self.web loadHTMLString:@"<meta name='viewport' content='width=device-width,initial-scale=1'><style>body{font:24px sans-serif;background:white;color:black}button{font-size:24px;margin-top:30px}</style><h1>Web</h1><p id='count'>WebCount: 0</p><button id='next' onclick=\"document.getElementById('count').textContent='WebCount: 1';requestAnimationFrame(report)\">Advance</button><script>function report(){window.webkit.messageHandlers.effect.postMessage({width:innerWidth,elements:['count','next'].map(id=>{let e=document.getElementById(id),r=e.getBoundingClientRect();return {text:e.textContent,interactive:id==='next',rect:[r.x,r.y,r.width,r.height]}})})}window.addEventListener('load',report)</script>" baseURL:nil];
    [self.window makeKeyAndVisible]; return YES;
}
- (void)userContentController:(WKUserContentController *)controller didReceiveScriptMessage:(WKScriptMessage *)message {
    (void)controller; NSDictionary *body=message.body; NSMutableArray *elements=[NSMutableArray new];
    double scale=self.web.bounds.size.width/[body[@"width"] doubleValue];
    for (NSDictionary *entry in body[@"elements"]) {
        NSArray *r=entry[@"rect"];CGRect rect=CGRectMake([r[0] doubleValue]*scale,[r[1] doubleValue]*scale,[r[2] doubleValue]*scale,[r[3] doubleValue]*scale);
        rect=[self.web convertRect:rect toView:self.window];
        [elements addObject:@{@"text":entry[@"text"],@"interactive":entry[@"interactive"],@"rect":@[@(rect.origin.x),@(rect.origin.y),@(rect.size.width),@(rect.size.height)]}];
    }
    NSData *json=[NSJSONSerialization dataWithJSONObject:@{@"viewport":@[@(self.window.bounds.size.width),@(self.window.bounds.size.height)],@"elements":elements} options:0 error:nil];
    fwrite("APP_INSPECTION ",1,15,stdout);fwrite(json.bytes,1,json.length,stdout);fputc('\n',stdout);fflush(stdout);
}

@end
int main(int argc,char **argv) { @autoreleasepool { return UIApplicationMain(argc,argv,nil,NSStringFromClass(WebDelegate.class)); } }
