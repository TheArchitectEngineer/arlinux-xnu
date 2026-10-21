/* Foundation's public URL-loading extension supplies Linux HTTP transport.
 * NSURLSession still owns tasks and delegate/completion delivery. No private
 * Network.framework policy object or successful NECP result is fabricated.
 * Buffered foreground requests only; background sessions/body streams, custom
 * trust/authentication challenges and full ATS exception rules are unsupported. */
#import <Foundation/Foundation.h>
#import <CoreFoundation/CoreFoundation.h>
#import <objc/runtime.h>
#include "ios-platform-abi.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static const struct arlinux_ios_platform_api *network;
@interface ARLinuxHTTPProtocol : NSURLProtocol
@property(nonatomic) uint64_t token;
@property(nonatomic) BOOL stopped;
@property(nonatomic,retain) NSTimer *timer;
@end
static BOOL insecure_http_allowed(NSURL *url)
{
    NSDictionary *policy=[[NSBundle mainBundle] objectForInfoDictionaryKey:@"NSAppTransportSecurity"];
    if([policy[@"NSAllowsArbitraryLoads"] boolValue])return YES;
    NSString *host=url.host.lowercaseString;
    BOOL local=[host isEqualToString:@"localhost"] || [host isEqualToString:@"127.0.0.1"] ||
        [host isEqualToString:@"::1"] || [host isEqualToString:@"[::1]"] ||
        [host hasSuffix:@".local"] || (host.length && [host rangeOfString:@"."].location==NSNotFound &&
                                                    [host rangeOfString:@":"].location==NSNotFound);
    return local && [policy[@"NSAllowsLocalNetworking"] boolValue];
}
static NSInteger url_error(uint32_t error)
{
    switch(error) {
    case AR_HTTP_TIMEOUT:return NSURLErrorTimedOut;
    case AR_HTTP_DNS:return NSURLErrorCannotFindHost;
    case AR_HTTP_CONNECT:return NSURLErrorCannotConnectToHost;
    case AR_HTTP_TLS:return NSURLErrorSecureConnectionFailed;
    case AR_HTTP_CANCELLED:return NSURLErrorCancelled;
    case AR_HTTP_TOO_LARGE:return NSURLErrorDataLengthExceedsMaximum;
    case AR_HTTP_INVALID:return NSURLErrorBadURL;
    default:return NSURLErrorNetworkConnectionLost;
    }
}
@implementation ARLinuxHTTPProtocol
+ (BOOL)canInitWithRequest:(NSURLRequest *)request
{
    NSString *scheme=request.URL.scheme.lowercaseString;
    return [scheme isEqualToString:@"http"] || [scheme isEqualToString:@"https"];
}
+ (NSURLRequest *)canonicalRequestForRequest:(NSURLRequest *)request{return request;}
- (void)fail:(NSInteger)code transport:(uint32_t)transport
{
    NSDictionary *info=@{NSURLErrorKey:self.request.URL,
                         @"ARLinuxTransportCode":@(transport)};
    [self.client URLProtocol:self didFailWithError:[NSError errorWithDomain:NSURLErrorDomain code:code userInfo:info]];
}
- (void)startLoading
{
    @synchronized(self) {
        if(self.stopped)return;
        NSURLRequest *request=self.request;
        if([request.URL.scheme.lowercaseString isEqualToString:@"http"] && !insecure_http_allowed(request.URL)) {
            [self fail:NSURLErrorAppTransportSecurityRequiresSecureConnection transport:0];return;
        }
        if(request.HTTPBodyStream || request.URL.user || request.URL.password) {
            [self fail:NSURLErrorUnsupportedURL transport:0];return;
        }
        NSString *method=request.HTTPMethod ?: @"GET";
        NSCharacterSet *controls=[NSCharacterSet controlCharacterSet];
        if(!method.length || [method rangeOfCharacterFromSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]].location!=NSNotFound ||
           [method rangeOfCharacterFromSet:controls].location!=NSNotFound) {
            [self fail:NSURLErrorBadURL transport:0];return;
        }
        NSMutableString *headers=[NSMutableString string];
        for(NSString *key in request.allHTTPHeaderFields) {
            NSString *value=request.allHTTPHeaderFields[key];
            if([key rangeOfCharacterFromSet:controls].location!=NSNotFound ||
               [key containsString:@":"] || [value rangeOfCharacterFromSet:controls].location!=NSNotFound) {
                [self fail:NSURLErrorBadURL transport:0];return;
            }
            [headers appendFormat:@"%@: %@\r\n",key,value];
        }
        NSTimeInterval seconds=request.timeoutInterval;
        if(!(seconds>0 && seconds<=300)) {[self fail:NSURLErrorUnsupportedURL transport:0];return;}
        NSData *body=request.HTTPBody;
        struct arlinux_http_request input={.url=request.URL.absoluteString.UTF8String,
            .method=method.UTF8String,.headers=headers.UTF8String,.body=body.bytes,
            .body_size=body.length,.timeout_ms=(uint32_t)(seconds*1000)+1};
        if(input.timeout_ms>300000)input.timeout_ms=300000;
        long token=network->http_submit(&input);
        fprintf(stderr,"IOS_HTTP_SUBMIT result=%ld\n",token);
        if(token<0){[self fail:NSURLErrorCannotLoadFromNetwork transport:0];return;}
        self.token=token;
        CFRunLoopPerformBlock(CFRunLoopGetMain(),kCFRunLoopCommonModes,^{
            @synchronized(self) {
                if(self.stopped)return;
                self.timer=[NSTimer timerWithTimeInterval:0.02 target:self selector:@selector(poll:)
                                                userInfo:nil repeats:YES];
                [[NSRunLoop mainRunLoop] addTimer:self.timer forMode:NSRunLoopCommonModes];
            }
        });
        CFRunLoopWakeUp(CFRunLoopGetMain());
    }
}
- (void)stopLoading
{
    @synchronized(self) {
        self.stopped=YES;
        [self.timer invalidate];self.timer=nil;
        if(self.token){network->http_release(self.token);self.token=0;}
    }
}
- (void)poll:(NSTimer *)timer
{
    (void)timer;
    @autoreleasepool { @synchronized(self) {
        if(self.stopped || !self.token)return;
        [[self retain] autorelease]; /* invalidating the timer releases its target */
        struct arlinux_http_response result={0};
        long ready=network->http_result(self.token,&result);
        if(!ready)return;
        [self.timer invalidate];self.timer=nil;
        NSData *data=ready>0 && !result.error ? [NSData dataWithBytes:result.body length:result.body_size] : nil;
        NSString *raw=ready>0 && result.headers ? [[[NSString alloc] initWithBytes:result.headers
            length:result.header_size encoding:NSISOLatin1StringEncoding] autorelease] : nil;
        network->http_release(self.token);self.token=0;
        fprintf(stderr,"IOS_HTTP_RESULT ready=%ld status=%u bytes=%llu error=%u transport=%u\n",
            ready,result.status,(unsigned long long)result.body_size,result.error,result.transport_code);
        if(ready<0 || result.error){[self fail:ready<0 ? NSURLErrorUnknown : url_error(result.error)
                                              transport:result.transport_code];return;}
        NSMutableDictionary *headers=[NSMutableDictionary dictionary];
        for(NSString *line in [raw componentsSeparatedByString:@"\r\n"]) {
            NSRange colon=[line rangeOfString:@":"];
            if(colon.location==NSNotFound)continue;
            NSString *name=[line substringToIndex:colon.location];
            NSString *value=[[line substringFromIndex:colon.location+1]
                stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceCharacterSet]];
            headers[name]=headers[name] ? [NSString stringWithFormat:@"%@, %@",headers[name],value] : value;
        }
        NSHTTPURLResponse *response=[[[NSHTTPURLResponse alloc] initWithURL:self.request.URL
            statusCode:result.status HTTPVersion:@"HTTP/1.1" headerFields:headers] autorelease];
        NSString *location=[response valueForHTTPHeaderField:@"Location"];
        if(location && (result.status==301 || result.status==302 || result.status==303 || result.status==307 || result.status==308)) {
            NSMutableURLRequest *redirect=[[self.request mutableCopy] autorelease];
            redirect.URL=[NSURL URLWithString:location relativeToURL:self.request.URL].absoluteURL;
            if(result.status==303 || ((result.status==301 || result.status==302) && [redirect.HTTPMethod isEqualToString:@"POST"])) {
                redirect.HTTPMethod=@"GET";redirect.HTTPBody=nil;
                [redirect setValue:nil forHTTPHeaderField:@"Content-Length"];
                [redirect setValue:nil forHTTPHeaderField:@"Content-Type"];
            }
            if(![redirect.URL.host isEqualToString:self.request.URL.host] ||
               ![redirect.URL.scheme isEqualToString:self.request.URL.scheme] ||
               ![redirect.URL.port isEqual:self.request.URL.port]) {
                [redirect setValue:nil forHTTPHeaderField:@"Authorization"];
                [redirect setValue:nil forHTTPHeaderField:@"Cookie"];
            }
            [self.client URLProtocol:self wasRedirectedToRequest:redirect redirectResponse:response];return;
        }
        [self.client URLProtocol:self didReceiveResponse:response cacheStoragePolicy:NSURLCacheStorageAllowed];
        if(self.stopped)return;
        if(data.length)[self.client URLProtocol:self didLoadData:data];
        if(!self.stopped)[self.client URLProtocolDidFinishLoading:self];
    }}
}
- (void)dealloc {[_timer invalidate];[_timer release];[super dealloc];}
@end
static id (*original_default)(id,SEL),(*original_ephemeral)(id,SEL);
static id configure(id value)
{
    NSURLSessionConfiguration *config=value;
    NSMutableArray *classes=[NSMutableArray arrayWithObject:[ARLinuxHTTPProtocol class]];
    for(Class cls in config.protocolClasses)if(cls!=[ARLinuxHTTPProtocol class])[classes addObject:cls];
    config.protocolClasses=classes;return config;
}
static id default_configuration(id cls,SEL selector){return configure(original_default(cls,selector));}
static id ephemeral_configuration(id cls,SEL selector){return configure(original_ephemeral(cls,selector));}
void arlinux_ios_network_start(const struct arlinux_ios_platform_api *api)
{
    network=api;
    if(!api->http_submit || !api->http_result || !api->http_release)abort();
    [NSURLProtocol registerClass:[ARLinuxHTTPProtocol class]];
    Method a=class_getClassMethod([NSURLSessionConfiguration class],@selector(defaultSessionConfiguration));
    Method b=class_getClassMethod([NSURLSessionConfiguration class],@selector(ephemeralSessionConfiguration));
    if(!a || !b || strcmp(method_getTypeEncoding(a),"@16@0:8") || strcmp(method_getTypeEncoding(b),"@16@0:8"))abort();
    original_default=(void *)method_getImplementation(a);original_ephemeral=(void *)method_getImplementation(b);
    method_setImplementation(a,(IMP)default_configuration);method_setImplementation(b,(IMP)ephemeral_configuration);
}
