#import "../shared/Screen.h"
@interface NotesDelegate : UIResponder <UIApplicationDelegate>
@property(nonatomic,strong) UIWindow *window; @property(nonatomic,strong) UILabel *result; @property(nonatomic,strong) UITextField *field;
@end
@implementation NotesDelegate
- (NSURL *)noteURL {
    NSURL *directory=[[[NSFileManager defaultManager] URLsForDirectory:NSDocumentDirectory inDomains:NSUserDomainMask] firstObject];
    return [directory URLByAppendingPathComponent:@"note.txt"];
}
- (BOOL)application:(UIApplication *)app didFinishLaunchingWithOptions:(NSDictionary *)options {
    (void)app; (void)options; UIView *view; self.window=Screen(@"Notes",&view);
    self.result=Label(view,@"Empty",85);
    NSString *saved=[NSString stringWithContentsOfURL:[self noteURL] encoding:NSUTF8StringEncoding error:nil];
    if (saved) self.result.text=[@"Saved: " stringByAppendingString:saved];
    self.field=[[UITextField alloc] initWithFrame:CGRectMake(20,145,280,40)];
    self.field.placeholder=@"Edit"; self.field.borderStyle=UITextBorderStyleRoundedRect; [view addSubview:self.field];
    Button(view,@"Save",self,@selector(save:),210); [self.window makeKeyAndVisible]; AppInspectionStart(self.window); return YES;
}
- (void)save:(id)sender {
    (void)sender; [self.field resignFirstResponder]; NSError *error=nil;
    BOOL ok=[self.field.text writeToURL:[self noteURL] atomically:YES encoding:NSUTF8StringEncoding error:&error];
    self.result.text=ok ? [@"Saved: " stringByAppendingString:self.field.text] : @"Save failed";
}
@end
int main(int argc,char **argv) { @autoreleasepool { return UIApplicationMain(argc,argv,nil,NSStringFromClass(NotesDelegate.class)); } }
