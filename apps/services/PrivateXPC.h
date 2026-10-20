#import <Foundation/Foundation.h>
/* iOS application-requested XPC services are a platform extension, not an App Store API.
 * These declarations describe the selected firmware ABI only. No implementation. */
@class NSXPCInterface, NSXPCConnection, NSXPCListener;
@protocol EchoProtocol
- (void)echo:(NSString *)message reply:(void (^)(NSString *))reply;
@end
@interface NSXPCInterface : NSObject
+ (instancetype)interfaceWithProtocol:(Protocol *)protocol;
@end
@interface NSXPCConnection : NSObject
- (instancetype)initWithServiceName:(NSString *)name;
@property(strong) NSXPCInterface *remoteObjectInterface;
@property(strong) NSXPCInterface *exportedInterface;
@property(strong) id exportedObject;
@property(copy) void (^invalidationHandler)(void);
- (id)remoteObjectProxyWithErrorHandler:(void (^)(NSError *))handler;
- (void)resume;
@end
@protocol NSXPCListenerDelegate
- (BOOL)listener:(NSXPCListener *)listener shouldAcceptNewConnection:(NSXPCConnection *)connection;
@end
@interface NSXPCListener : NSObject
+ (instancetype)serviceListener;
@property(weak) id<NSXPCListenerDelegate> delegate;
- (void)resume;
@end
