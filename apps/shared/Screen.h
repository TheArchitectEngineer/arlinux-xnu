#import <UIKit/UIKit.h>
static inline UILabel *Label(UIView *view, NSString *text, CGFloat y) {
    UILabel *label=[[UILabel alloc] initWithFrame:CGRectMake(20,y,280,45)];
    label.text=text; label.textColor=UIColor.blackColor; [view addSubview:label]; return label;
}
static inline void Button(UIView *view, NSString *title, id target, SEL action, CGFloat y) {
    UIButton *button=[UIButton buttonWithType:UIButtonTypeSystem];
    button.frame=CGRectMake(20,y,280,45); [button setTitle:title forState:UIControlStateNormal];
    [button addTarget:target action:action forControlEvents:UIControlEventTouchUpInside]; [view addSubview:button];
}
static inline UIWindow *Screen(NSString *title, UIView **content) {
    UIWindow *window=[[UIWindow alloc] initWithFrame:UIScreen.mainScreen.bounds];
    UIViewController *controller=[UIViewController new]; window.rootViewController=controller;
    *content=controller.view; (*content).backgroundColor=UIColor.whiteColor;
    Label(*content,title,30); return window;
}
