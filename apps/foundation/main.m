#import <Foundation/Foundation.h>
#include <stdio.h>
int main(void) { @autoreleasepool {
    NSDictionary *original=@{@"message":@"你好 café",@"values":@[@2,@3,@5]};
    NSError *error=nil;
    NSData *bytes=[NSJSONSerialization dataWithJSONObject:original options:0 error:&error];
    NSDictionary *decoded=bytes ? [NSJSONSerialization JSONObjectWithData:bytes options:0 error:&error] : nil;
    if (error || ![decoded isEqual:original]) return 21;
    @try { [NSException raise:@"Expected" format:@"real unwind"]; }
    @catch (NSException *exception) { if (![[exception name] isEqual:@"Expected"]) return 22; }
    __block BOOL fired=NO;
    [NSTimer scheduledTimerWithTimeInterval:0.02 repeats:NO block:^(NSTimer *timer) { (void)timer; fired=YES; }];
    NSDate *deadline=[NSDate dateWithTimeIntervalSinceNow:3];
    while (!fired && [deadline timeIntervalSinceNow]>0)
        [[NSRunLoop currentRunLoop] runMode:NSDefaultRunLoopMode beforeDate:deadline];
    if (!fired) return 23;
    NSInteger sum=0; for (NSNumber *value in decoded[@"values"]) sum+=value.integerValue;
    printf("Foundation JSON unicode exception timer: %ld\n",(long)sum); fflush(stdout);
    return getchar()=='q' ? 0 : 24;
} }
