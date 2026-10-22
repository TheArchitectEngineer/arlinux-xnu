/* UIDevice's platform identity uses Linux storage rather than an unavailable
 * Apple installation service. No app action or identifier constant is supplied.
 * Non-App-Store vendor scope: bundle ID excluding the last component, per
 * developer.apple.com/documentation/uikit/uidevice/identifierforvendor
 * Removing the shared vendor store resets this development installation ID.
 * App-Store vendor metadata and last-app uninstall tracking are not implemented.
 */
#import <UIKit/UIKit.h>
#import <objc/runtime.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ios-platform-abi.h"

static const struct arlinux_ios_platform_api *device_host;
static NSUUID *installation_identifier;
static NSUUID *vendor_identifier(UIDevice *device,SEL selector)
{
    (void)device;(void)selector;
    @synchronized([UIDevice class]) {
        if(installation_identifier)return installation_identifier;
        NSString *bundle=[NSBundle mainBundle].bundleIdentifier;
        NSRange separator=[bundle rangeOfString:@"." options:NSBackwardsSearch];
        NSString *vendor=separator.location==NSNotFound ? bundle : [bundle substringToIndex:separator.location];
        const char *name=vendor.UTF8String;
        if(!name || !*name)return nil;
        unsigned char bytes[16];
        long status=device_host->vendor_uuid(name,bytes,sizeof(bytes));
        fprintf(stderr,"IOS_VENDOR_IDENTIFIER status=%ld vendor=%.240s\n",status,name);
        if(status)return nil; /* Keep retry/error behavior, never fake success. */
        installation_identifier=[[NSUUID alloc] initWithUUIDBytes:bytes];
        return installation_identifier;
    }
}
void arlinux_ios_device_start(const struct arlinux_ios_platform_api *api)
{
    Method method=class_getInstanceMethod([UIDevice class],@selector(identifierForVendor));
    const char *encoding=method ? method_getTypeEncoding(method) : NULL;
    if(!api->vendor_uuid || !encoding || strcmp(encoding,"@16@0:8")) {
        fprintf(stderr,"IOS_DEVICE_BINDING_FAILED types=%s\n",encoding ?: "<absent>");abort();
    }
    device_host=api;
    method_setImplementation(method,(IMP)vendor_identifier);
}
