/* Exercise actual timed UIKit/Core Animation completion. Only the original
 * completion/delegate reports success; no timer or forced callback supplies it. */
#import <UIKit/UIKit.h>
#import <QuartzCore/QuartzCore.h>
#import "../shared/Inspect.h"
#include <stdio.h>
@interface AnimationDelegate : UIResponder <UIApplicationDelegate,CAAnimationDelegate>
@property(nonatomic,strong) UIWindow *window;
@property(nonatomic,strong) UIView *panel;
@property(nonatomic,strong) UILabel *result;
@property(nonatomic) CFTimeInterval started;
@property(nonatomic) BOOL running,observedStart;
@property(nonatomic) NSUInteger viewCompletions,layerCompletions;
@end
@implementation AnimationDelegate
- (BOOL)application:(UIApplication *)application didFinishLaunchingWithOptions:(NSDictionary *)options {
    (void)application;(void)options;
    self.window=[[UIWindow alloc] initWithFrame:UIScreen.mainScreen.bounds];
    UIViewController *controller=[UIViewController new];UIView *view=controller.view;
    view.backgroundColor=UIColor.whiteColor;
    UILabel *title=[[UILabel alloc] initWithFrame:CGRectMake(20,25,320,40)];
    title.text=@"Animation probe";title.textColor=UIColor.blackColor;[view addSubview:title];
    NSArray *titles=@[@"Animate view",@"Animate layer"];
    for(NSUInteger i=0;i<titles.count;i++) {
        UIButton *button=[UIButton buttonWithType:UIButtonTypeSystem];
        button.frame=CGRectMake(20,75+i*55,280,45);
        [button setTitle:titles[i] forState:UIControlStateNormal];
        [button addTarget:self action:i ? @selector(animateLayer:) : @selector(animateView:)
            forControlEvents:UIControlEventTouchUpInside];[view addSubview:button];
    }
    self.result=[[UILabel alloc] initWithFrame:CGRectMake(20,185,320,40)];
    self.result.text=@"Ready";self.result.textColor=UIColor.blackColor;[view addSubview:self.result];
    self.panel=[[UIView alloc] initWithFrame:CGRectMake(40,250,120,100)];
    self.panel.backgroundColor=UIColor.redColor;[view addSubview:self.panel];
    self.window.rootViewController=controller;[self.window makeKeyAndVisible];
    AppInspectionStart(self.window);return YES;
}
- (void)animateView:(id)sender {
    (void)sender;if(self.running)return;self.running=YES;
    self.panel.frame=CGRectMake(40,250,120,100);self.panel.backgroundColor=UIColor.redColor;
    self.result.text=@"UIView pending";self.started=CACurrentMediaTime();
    [UIView animateWithDuration:0.35 animations:^{
        self.panel.frame=CGRectMake(140,250,120,100);self.panel.backgroundColor=UIColor.greenColor;
    } completion:^(BOOL finished) {
        CFTimeInterval elapsed=CACurrentMediaTime()-self.started;self.running=NO;
        fprintf(stderr,"ANIMATION_PROBE_VIEW finished=%d elapsed=%.6f\n",finished,elapsed);
        if(finished && elapsed>=0.15)self.result.text=[NSString stringWithFormat:@"UIView finished: %lu",++self.viewCompletions];
        else self.result.text=@"UIView incomplete";
    }];
}
- (void)animateLayer:(id)sender {
    (void)sender;if(self.running)return;self.running=YES;self.observedStart=NO;
    self.result.text=@"CA pending";self.started=CACurrentMediaTime();
    [CATransaction begin];[CATransaction setDisableActions:YES];
    self.panel.frame=CGRectMake(140,250,120,100);self.panel.backgroundColor=UIColor.greenColor;
    [CATransaction commit];
    CABasicAnimation *animation=[CABasicAnimation animationWithKeyPath:@"position.x"];
    animation.fromValue=@100;animation.toValue=@200;animation.duration=0.35;animation.delegate=self;
    [self.panel.layer addAnimation:animation forKey:@"probe-position"];
}
- (void)animationDidStart:(CAAnimation *)animation {
    (void)animation;self.observedStart=YES;
    fprintf(stderr,"ANIMATION_PROBE_CA_START elapsed=%.6f\n",CACurrentMediaTime()-self.started);
}
- (void)animationDidStop:(CAAnimation *)animation finished:(BOOL)finished {
    (void)animation;CFTimeInterval elapsed=CACurrentMediaTime()-self.started;self.running=NO;
    fprintf(stderr,"ANIMATION_PROBE_CA_STOP finished=%d started=%d elapsed=%.6f\n",finished,self.observedStart,elapsed);
    if(finished && self.observedStart && elapsed>=0.15)
        self.result.text=[NSString stringWithFormat:@"CA finished: %lu",++self.layerCompletions];
    else self.result.text=@"CA incomplete";
}
@end
int main(int argc,char **argv) {
    @autoreleasepool {return UIApplicationMain(argc,argv,nil,@"AnimationDelegate");}
}
