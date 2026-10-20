#import "../sdk/XPC.h"
extern void _exit(int);
@interface ARWorker : NSObject <ARJourneyWorker,NSXPCListenerDelegate> @end
@implementation ARWorker
- (void)ping:(NSString *)value reply:(void (^)(NSString *))reply
{ (void)value; reply(@"pong"); }
- (BOOL)listener:(NSXPCListener *)listener shouldAcceptNewConnection:(NSXPCConnection *)connection
{
    (void)listener;
    [connection setExportedInterface:[NSXPCInterface interfaceWithProtocol:@protocol(ARJourneyWorker)]];
    [connection setExportedObject:self];
    [connection setInvalidationHandler:^{ _exit(0); }];
    [connection resume]; return 1;
}
@end
int main(void)
{
    @autoreleasepool {
        NSXPCListener *listener=[NSXPCListener serviceListener];
        [listener setDelegate:[[ARWorker alloc] init]];
        [listener resume];
        return 1; /* Apple's service listener must not return normally. */
    }
}
