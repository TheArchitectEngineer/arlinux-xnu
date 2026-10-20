#import "../shared/Screen.h"
@interface ArtView : UIView
@property BOOL alternate;
@end
@implementation ArtView
- (void)drawRect:(CGRect)rect {
    (void)rect; CGContextRef context=UIGraphicsGetCurrentContext();
    CGContextSetRGBFillColor(context,self.alternate ? 0 : 1,self.alternate ? 1 : 0,0,1);
    CGContextFillRect(context,CGRectMake(0,0,240,100));
    CGContextSetRGBFillColor(context,0,0,1,1); CGContextFillEllipseInRect(context,CGRectMake(80,20,60,60));
}
@end
@interface CanvasDelegate : UIResponder <UIApplicationDelegate>
@property(strong) UIWindow *window; @property(strong) UILabel *result; @property(strong) ArtView *art;
@end
@implementation CanvasDelegate
- (BOOL)application:(UIApplication *)app didFinishLaunchingWithOptions:(NSDictionary *)options {
    (void)app; (void)options; UIView *view; self.window=Screen(@"Canvas",&view); self.result=Label(view,@"Red",80);
    self.art=[[ArtView alloc] initWithFrame:CGRectMake(30,140,240,100)]; [view addSubview:self.art];
    Button(view,@"Paint",self,@selector(paint:),280); [self.window makeKeyAndVisible]; return YES;
}
- (void)paint:(id)sender { (void)sender; self.art.alternate=YES; [self.art setNeedsDisplay]; self.result.text=@"Green"; }
@end
int main(int argc,char **argv) { @autoreleasepool { return UIApplicationMain(argc,argv,nil,NSStringFromClass(CanvasDelegate.class)); } }
