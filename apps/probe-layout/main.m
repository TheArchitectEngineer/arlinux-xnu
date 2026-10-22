/* Real UIKit deferred layout. Only layoutSubviews publishes the requested
 * result; external input requests layout through the public setNeedsLayout API.
 * No fixture/harness call to layoutIfNeeded or layoutSubviews is permitted. */
#import <UIKit/UIKit.h>
#import <QuartzCore/QuartzCore.h>
#import "../shared/Inspect.h"
#include <stdio.h>

@interface LayoutSurface : UIView
@property(nonatomic,strong) UILabel *result;
@property(nonatomic,strong) UIView *panel;
@property(nonatomic) BOOL inset;
@end
@implementation LayoutSurface
- (instancetype)initWithFrame:(CGRect)frame {
    if((self=[super initWithFrame:frame])) {
        self.backgroundColor=UIColor.whiteColor;
        UILabel *title=[[UILabel alloc] initWithFrame:CGRectMake(20,30,300,45)];
        title.text=@"Layout probe";title.textColor=UIColor.blackColor;[self addSubview:title];
        _result=[[UILabel alloc] initWithFrame:CGRectMake(20,160,320,45)];
        _result.text=@"Waiting for layout";_result.textColor=UIColor.blackColor;[self addSubview:_result];
        UIButton *button=[UIButton buttonWithType:UIButtonTypeSystem];
        button.frame=CGRectMake(20,90,280,50);
        [button setTitle:@"Change inset" forState:UIControlStateNormal];
        [button addTarget:self action:@selector(changeInset:) forControlEvents:UIControlEventTouchUpInside];
        [self addSubview:button];
        _panel=[[UIView alloc] initWithFrame:CGRectMake(20,220,200,120)];
        _panel.backgroundColor=UIColor.redColor;[self addSubview:_panel];
    }
    return self;
}
- (void)changeInset:(id)sender {
    (void)sender;self.inset=!self.inset;
    self.result.text=@"Waiting for requested layout";
    [self setNeedsLayout];
    fprintf(stderr,"LAYOUT_PROBE_REQUEST inset=%d pending=%d\n",self.inset,self.layer.needsLayout);
}
- (void)layoutSubviews {
    [super layoutSubviews];
    CGFloat inset=self.inset ? 60 : 20;
    self.panel.frame=CGRectMake(inset,220,MAX(1,CGRectGetWidth(self.bounds)-2*inset),120);
    self.panel.backgroundColor=self.inset ? UIColor.greenColor : UIColor.redColor;
    self.result.text=self.inset ? @"Layout ready: inset" : @"Layout ready: normal";
    fprintf(stderr,"LAYOUT_PROBE_PASS inset=%d width=%g panel_width=%g\n",
        self.inset,CGRectGetWidth(self.bounds),CGRectGetWidth(self.panel.frame));
}
@end
@interface LayoutDelegate : UIResponder <UIApplicationDelegate>
@property(nonatomic,strong) UIWindow *window;
@end
@implementation LayoutDelegate
- (BOOL)application:(UIApplication *)application didFinishLaunchingWithOptions:(NSDictionary *)options {
    (void)application;(void)options;
    self.window=[[UIWindow alloc] initWithFrame:UIScreen.mainScreen.bounds];
    UIViewController *controller=[UIViewController new];
    controller.view=[[LayoutSurface alloc] initWithFrame:UIScreen.mainScreen.bounds];
    self.window.rootViewController=controller;
    [self.window makeKeyAndVisible];AppInspectionStart(self.window);return YES;
}
@end
int main(int argc,char **argv) {
    @autoreleasepool {return UIApplicationMain(argc,argv,nil,@"LayoutDelegate");}
}
