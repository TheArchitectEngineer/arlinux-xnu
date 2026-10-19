/* ARHello 1.0: an actual programmatic UIKit application. No simulated window,
 * substitute event loop or host-drawn replica is used for acceptance. */
#import "UIKit-interfaces.h"

enum { TextAlignmentCenter=1, ButtonTypeSystem=1, ControlStateNormal=0,
       ControlEventTouchUpInside=1UL<<6 };
@interface ARHelloDelegate : UIResponder {
    UIWindow *_window;
    UILabel *_label;
    NSUInteger _count;
}
- (BOOL)application:(UIApplication *)application didFinishLaunchingWithOptions:(id)options;
- (void)increment:(id)sender;
@end
@implementation ARHelloDelegate
- (BOOL)application:(UIApplication *)application didFinishLaunchingWithOptions:(id)options
{
    (void)application; (void)options;
    CGRect bounds=[[UIScreen mainScreen] bounds];
    _window=[[UIWindow alloc] initWithFrame:bounds];
    UIViewController *controller=[[UIViewController alloc] init];
    UIView *view=[controller view];
    [view setBackgroundColor:[UIColor whiteColor]];
    _label=[[UILabel alloc] initWithFrame:(CGRect){{20,100},{280,60}}];
    [_label setText:@"ARHello 1.0 — Count: 0"];
    [_label setTextColor:[UIColor blackColor]];
    [_label setTextAlignment:TextAlignmentCenter];
    [view addSubview:_label];
    UIButton *button=[UIButton buttonWithType:ButtonTypeSystem];
    [button setFrame:(CGRect){{20,180},{280,60}}];
    [button setTitle:@"Increment" forState:ControlStateNormal];
    [button addTarget:self action:@selector(increment:) forControlEvents:ControlEventTouchUpInside];
    [view addSubview:button];
    [_window setRootViewController:controller];
    [_window makeKeyAndVisible];
    const char ready[]="ARHELLO_READY version=1.0 UIKit window made visible\n";
    write(1,ready,sizeof(ready)-1);
    return 1;
}
- (void)increment:(id)sender
{
    (void)sender;
    _count++;
    [_label setText:[NSString stringWithFormat:@"ARHello 1.0 — Count: %lu",_count]];
    const char event[]="ARHELLO_INPUT increment callback\n";
    write(1,event,sizeof(event)-1);
}
@end
int main(int argc,char **argv)
{
    @autoreleasepool {
        const char entry[]="ARHELLO_MAIN version=1.0 entering real UIApplicationMain\n";
        write(1,entry,sizeof(entry)-1);
        return UIApplicationMain(argc,argv,nil,@"ARHelloDelegate");
    }
}
