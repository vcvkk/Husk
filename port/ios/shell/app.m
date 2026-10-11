// SPDX-License-Identifier: GPL-2.0-or-later
// The iOS app an Android app becomes: ART with the framework (port/framework) runs the APK's launcher
// activity; what it draws is shown full screen, a frame each display refresh, and touches go to it.
//
// The bundle (port/ios/pack-app.sh):
//   <exe>, Frameworks/lib*.dylib     ART's runtime, the class library's natives
//   art/boot.classpath, art/jars/    the boot class path, the framework's jar last
//   art/arm64/boot*.{art,oat,vdex}   the boot image; boot*.oat.dylib its code, signed with the app
//   app/base.apk                     the APK; app/oat/arm64/base.odex{,.vdex,.dylib} its code
//   app/framework-res.apk            the platform's resources (themes and the rest apps name as android:...)
//
// The log goes to Documents/husk-port.log (the Files app).
#import <QuartzCore/QuartzCore.h>
#import <UIKit/UIKit.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <unistd.h>

#include "husk-framework.h"
#include "husk-launch.h"

static NSString *gBundle;

/* the framework's web views (port/framework/jni/web.m): WKWebViews in a container under the app's frames */
void tl_web_attach(void *screen_view, void *container, float scale, bool fresh);
bool tl_web_hit(float x, float y);

static void Log(void)
{
    NSString *docs = NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES).firstObject;
    int fd = open([docs stringByAppendingPathComponent:@"husk-port.log"].UTF8String, O_WRONLY | O_CREAT | O_TRUNC | O_APPEND, 0644);
    if (fd < 0) return;
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);
    dup2(fd, STDOUT_FILENO);
    dup2(fd, STDERR_FILENO);
    close(fd);
}

static void *AppMain(void *unused)
{
    (void)unused;
    pthread_setname_np("main");
    NSString *art = [gBundle stringByAppendingPathComponent:@"art"];
    NSString *data = [NSTemporaryDirectory() stringByAppendingPathComponent:@"android-data"];
    JavaVM *vm; JNIEnv *env;
    if (!husk_vm_start(art.UTF8String, data.UTF8String, NULL, 0, &vm, &env)) return NULL;
    husk_app_run(env);
    (*vm)->DetachCurrentThread(vm);
    return NULL;
}

// ---- the screen: the app's latest frame as the layer's contents; touches to it

@interface FrameView : UIView
@end

@implementation FrameView {
    CADisplayLink *_link;
    NSMapTable<UITouch *, NSNumber *> *_ids;
    int _nextId;
}

- (instancetype)initWithFrame:(CGRect)frame
{
    if ((self = [super initWithFrame:frame])) {
        self.multipleTouchEnabled = YES;
        self.backgroundColor = UIColor.blackColor;
        self.layer.contentsGravity = kCAGravityResize;
        _ids = [NSMapTable strongToStrongObjectsMapTable];
        _link = [CADisplayLink displayLinkWithTarget:self selector:@selector(nextFrame:)];
        if (@available(iOS 15.0, *)) _link.preferredFrameRateRange = CAFrameRateRangeMake(60, 120, 120);
        [_link addToRunLoop:NSRunLoop.mainRunLoop forMode:NSRunLoopCommonModes];
    }
    return self;
}

- (void)nextFrame:(CADisplayLink *)link
{
    const uint8_t *px; int w, h; bool any;
    if (!husk_frame_take(&px, &w, &h, &any)) return;
    CGImageRef img = NULL;
    if (any && w > 0 && h > 0) {
        CFDataRef d = CFDataCreate(NULL, px, (CFIndex)w * h * 4);   // the frame stays the app's
        CGDataProviderRef p = CGDataProviderCreateWithCFData(d);
        CGColorSpaceRef cs = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
        img = CGImageCreate((size_t)w, (size_t)h, 8, 32, (size_t)w * 4, cs,
                            kCGImageAlphaPremultipliedLast | kCGBitmapByteOrder32Big, p, NULL, false, kCGRenderingIntentDefault);
        CGColorSpaceRelease(cs); CGDataProviderRelease(p); CFRelease(d);
    }
    husk_frame_done();
    [CATransaction begin];
    [CATransaction setDisableActions:YES];
    self.layer.contents = (__bridge id)img;
    [CATransaction commit];
    if (img) CGImageRelease(img);
}

// a touch on a web view's page goes to it, in the container below
- (UIView *)hitTest:(CGPoint)p withEvent:(UIEvent *)e
{
    if (tl_web_hit((float)p.x, (float)p.y)) return nil;
    return [super hitTest:p withEvent:e];
}

- (void)send:(NSSet<UITouch *> *)touches phase:(int)phase
{
    CGFloat s = UIScreen.mainScreen.scale;   // pixels, as the frame has them
    for (UITouch *t in touches) {
        NSNumber *n = [_ids objectForKey:t];
        if (!n) { n = @(_nextId++ % 16); [_ids setObject:n forKey:t]; }
        CGPoint p = [t locationInView:self];
        husk_app_touch(phase, n.intValue, (float)(p.x * s), (float)(p.y * s));
        if (phase >= 2) [_ids removeObjectForKey:t];
    }
}
- (void)touchesBegan:(NSSet<UITouch *> *)t withEvent:(UIEvent *)e { [self send:t phase:0]; }
- (void)touchesMoved:(NSSet<UITouch *> *)t withEvent:(UIEvent *)e { [self send:t phase:1]; }
- (void)touchesEnded:(NSSet<UITouch *> *)t withEvent:(UIEvent *)e { [self send:t phase:2]; }
- (void)touchesCancelled:(NSSet<UITouch *> *)t withEvent:(UIEvent *)e { [self send:t phase:3]; }

- (void)safeAreaInsetsDidChange
{
    [super safeAreaInsetsDidChange];
    CGFloat s = UIScreen.mainScreen.scale;
    UIEdgeInsets i = self.safeAreaInsets;
    husk_app_insets((int)(i.left * s), (int)(i.top * s), (int)(i.right * s), (int)(i.bottom * s), 0);
}
@end

@interface FrameController : UIViewController
@end
@implementation FrameController
- (void)loadView
{
    CGRect r = UIScreen.mainScreen.bounds;
    UIView *root = [[UIView alloc] initWithFrame:r];
    root.backgroundColor = UIColor.blackColor;
    UIView *webs = [[UIView alloc] initWithFrame:r];
    webs.backgroundColor = UIColor.blackColor;
    webs.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
    FrameView *frames = [[FrameView alloc] initWithFrame:r];
    frames.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
    [root addSubview:webs];
    [root addSubview:frames];
    self.view = root;
    tl_web_attach((__bridge void *)frames, (__bridge void *)webs, (float)UIScreen.mainScreen.scale, true);
}
- (BOOL)prefersStatusBarHidden { return NO; }
- (BOOL)prefersHomeIndicatorAutoHidden { return NO; }
@end

// ---- the app

static void OpenURL(const char *url)
{
    NSString *s = @(url);
    dispatch_async(dispatch_get_main_queue(), ^{
        NSURL *u = [NSURL URLWithString:s];
        if (u) [UIApplication.sharedApplication openURL:u options:@{} completionHandler:nil];
    });
}
static void SetClipboard(const char *text) { NSString *s = @(text); dispatch_async(dispatch_get_main_queue(), ^{ UIPasteboard.generalPasteboard.string = s; }); }
static char *GetClipboard(void)
{
    __block NSString *s;
    dispatch_sync(dispatch_get_main_queue(), ^{ s = UIPasteboard.generalPasteboard.string; });
    return s ? strdup(s.UTF8String) : NULL;
}

@interface AppDelegate : UIResponder <UIApplicationDelegate>
@property(strong, nonatomic) UIWindow *window;
@end

@implementation AppDelegate
- (BOOL)application:(UIApplication *)app didFinishLaunchingWithOptions:(NSDictionary *)opts
{
    Log();
    gBundle = NSBundle.mainBundle.bundlePath;
    self.window = [[UIWindow alloc] initWithFrame:UIScreen.mainScreen.bounds];
    self.window.rootViewController = [FrameController new];
    [self.window makeKeyAndVisible];

    UIScreen *screen = UIScreen.mainScreen;
    CGFloat scale = screen.scale;
    NSString *support = NSSearchPathForDirectoriesInDomains(NSApplicationSupportDirectory, NSUserDomainMask, YES).firstObject;
    NSString *docs = NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES).firstObject;
    NSString *data = [support stringByAppendingPathComponent:@"app"];
    [NSFileManager.defaultManager createDirectoryAtPath:data withIntermediateDirectories:YES attributes:nil error:nil];
    husk_app_config cfg = {
        .apk = [gBundle stringByAppendingPathComponent:@"app/base.apk"].UTF8String,
        .framework_res = [gBundle stringByAppendingPathComponent:@"app/framework-res.apk"].UTF8String,
        .data = data.UTF8String,
        .external = docs.UTF8String,
        .width = (int)(screen.bounds.size.width * scale),
        .height = (int)(screen.bounds.size.height * scale),
        .density = (float)scale,
        .night_mode = self.window.traitCollection.userInterfaceStyle == UIUserInterfaceStyleDark,
        .open_url = OpenURL,
        .set_clipboard = SetClipboard,
        .get_clipboard = GetClipboard,
    };
    husk_app_configure(&cfg);
    UIEdgeInsets i = self.window.safeAreaInsets;
    husk_app_insets((int)(i.left * scale), (int)(i.top * scale), (int)(i.right * scale), (int)(i.bottom * scale), 0);

    // ART wants a big stack; the main thread's belongs to UIKit
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 64 << 20);
    pthread_t t;
    pthread_create(&t, &attr, AppMain, NULL);
    return YES;
}
@end

int main(int argc, char *argv[])
{
    @autoreleasepool {
        return UIApplicationMain(argc, argv, nil, NSStringFromClass(AppDelegate.class));
    }
}
