/* Declaration-only SDK subset for ARHello 1.0. Implementations are supplied
 * exclusively by the selected firmware's UIKit/Foundation/Objective-C runtime.
 * LP64 iOS ABI: CGFloat is double, BOOL is signed char, NSInteger is long.
 * No framework object layouts or method implementations are reproduced here. */
typedef signed char BOOL;
typedef unsigned long NSUInteger;
typedef long NSInteger;
typedef double CGFloat;
typedef struct { CGFloat x,y; } CGPoint;
typedef struct { CGFloat width,height; } CGSize;
typedef struct { CGPoint origin; CGSize size; } CGRect;
typedef struct objc_selector *SEL;
typedef struct objc_class *Class;
#define nil ((id)0)
__attribute__((objc_root_class))
@interface NSObject
+ (id)alloc;
- (id)init;
+ (Class)class;
@end
@interface NSString : NSObject
+ (id)stringWithFormat:(NSString *)format,...;
@end
@interface UIResponder : NSObject @end
@interface UIColor : NSObject
+ (id)whiteColor;
+ (id)blackColor;
@end
@interface UIView : UIResponder
- (id)initWithFrame:(CGRect)frame;
- (void)setBackgroundColor:(UIColor *)color;
- (void)addSubview:(UIView *)view;
- (void)setFrame:(CGRect)frame;
@end
@interface UILabel : UIView
- (void)setText:(NSString *)text;
- (void)setTextColor:(UIColor *)color;
- (void)setTextAlignment:(NSInteger)alignment;
@end
@interface UIButton : UIView
+ (id)buttonWithType:(NSInteger)type;
- (void)setTitle:(NSString *)title forState:(NSUInteger)state;
- (void)addTarget:(id)target action:(SEL)action forControlEvents:(NSUInteger)events;
@end
@interface UIScreen : NSObject
+ (id)mainScreen;
- (CGRect)bounds;
@end
@interface UIViewController : UIResponder
- (UIView *)view;
@end
@interface UIWindow : UIView
- (void)setRootViewController:(UIViewController *)controller;
- (void)makeKeyAndVisible;
@end
@interface UIApplication : UIResponder @end
extern int UIApplicationMain(int,char **,NSString *,NSString *);
extern long write(int,const void *,unsigned long);
