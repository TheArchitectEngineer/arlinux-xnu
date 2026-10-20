#import "../shared/Screen.h"
@interface EchoExtension : UIViewController
@property(nonatomic,strong) UILabel *result;
@property(nonatomic,copy) NSString *payload;
@end
@implementation EchoExtension
- (void)viewDidLoad {
    [super viewDidLoad]; self.view.backgroundColor=UIColor.whiteColor; Label(self.view,@"EchoExtension",30);
    self.result=Label(self.view,@"Waiting",85); Button(self.view,@"Reply",self,@selector(reply:),160);
    NSExtensionItem *item=[self.extensionContext.inputItems firstObject];
    NSItemProvider *provider=[item.attachments firstObject];
    [provider loadItemForTypeIdentifier:@"public.plain-text" options:nil completionHandler:^(id value,NSError *error) {
        dispatch_async(dispatch_get_main_queue(),^{
            self.payload=!error && [value isKindOfClass:NSString.class] ? value : nil;
            self.result.text=self.payload ?: @"Payload failed";
        });
    }];
}
- (void)reply:(id)sender {
    (void)sender; if (!self.payload) return;
    NSExtensionItem *item=[NSExtensionItem new]; item.attributedContentText=[[NSAttributedString alloc] initWithString:self.payload];
    [self.extensionContext completeRequestReturningItems:@[item] completionHandler:nil];
}
@end
