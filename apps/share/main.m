#import "../shared/Screen.h"
@interface ShareDelegate : UIResponder <UIApplicationDelegate>
@property(nonatomic,strong) UIWindow *window;
@property(nonatomic,strong) UILabel *result;
@end
@implementation ShareDelegate
- (BOOL)application:(UIApplication *)app didFinishLaunchingWithOptions:(NSDictionary *)options {
    (void)app; (void)options; UIView *view; self.window=Screen(@"Share",&view); self.result=Label(view,@"Ready",85);
    Button(view,@"Send",self,@selector(send:),160); [self.window makeKeyAndVisible]; AppInspectionStart(self.window); return YES;
}
- (void)send:(id)sender {
    (void)sender;
    UIActivityViewController *activity=[[UIActivityViewController alloc] initWithActivityItems:@[@"payload-42"] applicationActivities:nil];
    activity.completionWithItemsHandler=^(UIActivityType type,BOOL completed,NSArray *items,NSError *error) {
        (void)type;
        NSExtensionItem *item=[items firstObject];
        NSString *text=[item isKindOfClass:NSExtensionItem.class] ? item.attributedContentText.string : nil;
        self.result.text=completed && !error && text ? [@"Echo: " stringByAppendingString:text] : @"Share failed";
    };
    activity.popoverPresentationController.sourceView=self.window.rootViewController.view;
    activity.popoverPresentationController.sourceRect=CGRectMake(20,160,280,45);
    [self.window.rootViewController presentViewController:activity animated:YES completion:nil];
}
@end
int main(int argc,char **argv) { @autoreleasepool { return UIApplicationMain(argc,argv,nil,NSStringFromClass(ShareDelegate.class)); } }
