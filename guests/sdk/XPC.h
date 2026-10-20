/* Declaration-only public Foundation XPC interfaces. No RPC implementation.
 * See Apple's Creating XPC Services / NSXPCConnection / NSXPCListener docs. */
#import "UIKit.h"
@class NSError, NSXPCConnection, NSXPCListener;
@protocol ARJourneyWorker
- (void)ping:(NSString *)value reply:(void (^)(NSString *))reply;
@end
@protocol NSXPCListenerDelegate
- (BOOL)listener:(NSXPCListener *)listener shouldAcceptNewConnection:(NSXPCConnection *)connection;
@end
@interface NSXPCInterface : NSObject
+ (id)interfaceWithProtocol:(Protocol *)protocol;
@end
@interface NSXPCConnection : NSObject
- (id)initWithServiceName:(NSString *)name;
- (void)setRemoteObjectInterface:(NSXPCInterface *)interface;
- (void)setExportedInterface:(NSXPCInterface *)interface;
- (void)setExportedObject:(id)object;
- (void)setInvalidationHandler:(void (^)(void))handler;
- (id)remoteObjectProxyWithErrorHandler:(void (^)(NSError *))handler;
- (void)resume;
@end
@interface NSXPCListener : NSObject
+ (id)serviceListener;
- (void)setDelegate:(id<NSXPCListenerDelegate>)delegate;
- (void)resume;
@end
extern struct dispatch_queue_s _dispatch_main_q;
static inline void *dispatch_get_main_queue(void) { return &_dispatch_main_q; }
extern void dispatch_async(void *,void (^)(void));
