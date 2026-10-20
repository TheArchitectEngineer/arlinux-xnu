#import "PrivateXPC.h"
#include <unistd.h>
@interface EchoWorker : NSObject <EchoProtocol,NSXPCListenerDelegate> @end
@implementation EchoWorker
- (void)echo:(NSString *)message reply:(void (^)(NSString *))reply { reply(message); }
- (BOOL)listener:(NSXPCListener *)listener shouldAcceptNewConnection:(NSXPCConnection *)connection {
    (void)listener; connection.exportedInterface=[NSXPCInterface interfaceWithProtocol:@protocol(EchoProtocol)];
    connection.exportedObject=self; connection.invalidationHandler=^{ _exit(0); }; [connection resume]; return YES;
}
@end
int main(void) { @autoreleasepool {
    NSXPCListener *listener=[NSXPCListener serviceListener]; EchoWorker *worker=[EchoWorker new];
    listener.delegate=worker; [listener resume]; return 1;
} }
