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

/* the screen's safe area and the keyboard's height, in pixels, as husk_app_insets takes them */
static int gInsets[4], gKeyboard;
static void SendInsets(void) { husk_app_insets(gInsets[0], gInsets[1], gInsets[2], gInsets[3], gKeyboard); }

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

// HUSK_FRAME_STATS: how many frames reach the screen, and what putting one there costs, every 2 s in the log
static bool gStats;
static double gStatStart, gStatTime, gStatMax; static int gStatFrames, gStatTicks;
static void FrameStat(double t0, double t1)
{
    double dt = t1 - t0;
    gStatFrames++; gStatTime += dt; if (dt > gStatMax) gStatMax = dt;
    if (gStatStart == 0) gStatStart = t0;
    if (t1 - gStatStart >= 2) {
        fprintf(stderr, "FrameStats(host): %d frames shown of %d display refreshes in %.1f s: %.2f ms each to put on screen, worst %.2f ms\n",
                gStatFrames, gStatTicks, t1 - gStatStart, gStatTime / gStatFrames * 1e3, gStatMax * 1e3);
        gStatStart = t1; gStatFrames = gStatTicks = 0; gStatTime = gStatMax = 0;
    }
}

- (void)nextFrame:(CADisplayLink *)link
{
    husk_app_vsync();                       // the app's Choreographer draws its next frame now, when it wants one
    const uint8_t *px; int w, h; bool any;
    if (gStats) gStatTicks++;
    double t0 = gStats ? CACurrentMediaTime() : 0;
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
    if (gStats) FrameStat(t0, CACurrentMediaTime());
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
    gInsets[0] = (int)(i.left * s); gInsets[1] = (int)(i.top * s); gInsets[2] = (int)(i.right * s); gInsets[3] = (int)(i.bottom * s);
    SendInsets();
}
@end

// ---- the keyboard: iOS's own (and the keyboards the user installed), typing into the focused view's input connection

@interface KeyInput : UIView <UIKeyInput>
@property(nonatomic) UIKeyboardType keyboardType;
@property(nonatomic) UIReturnKeyType returnKeyType;
@property(nonatomic) UITextAutocapitalizationType autocapitalizationType;
@property(nonatomic) UITextAutocorrectionType autocorrectionType;
@property(nonatomic) UITextSpellCheckingType spellCheckingType;
@property(nonatomic, getter=isSecureTextEntry) BOOL secureTextEntry;
@property(nonatomic) BOOL hiding;          // the app asked for it to go: not the user closing it
@end

@implementation KeyInput
- (BOOL)canBecomeFirstResponder { return YES; }
- (BOOL)hasText { return YES; }            // backspace always reaches the app, whose field knows what is there
- (void)insertText:(NSString *)text
{
    // the return key (and lines pasted in) as Android's editor action; the rest committed as typed
    NSArray<NSString *> *lines = [text componentsSeparatedByString:@"\n"];
    for (NSUInteger i = 0; i < lines.count; i++) {
        if (i > 0) husk_app_text_action();
        if (lines[i].length) husk_app_text(lines[i].UTF8String);
    }
}
- (void)deleteBackward { husk_app_text_delete(); }

// Android's EditorInfo (inputType, imeOptions) as iOS's text input traits
- (void)configureType:(int)type options:(int)ime
{
    int cls = type & 0xF, variation = type & 0xFF0, flags = type & 0xFFF000;
    UIKeyboardType kb = UIKeyboardTypeDefault;
    BOOL secure = NO;
    if (cls == 2) {                          // TYPE_CLASS_NUMBER
        kb = (flags & 0x2000) ? UIKeyboardTypeDecimalPad : (flags & 0x1000) ? UIKeyboardTypeNumbersAndPunctuation : UIKeyboardTypeNumberPad;
        secure = variation == 0x10;          // TYPE_NUMBER_VARIATION_PASSWORD
    } else if (cls == 3) kb = UIKeyboardTypePhonePad;
    else if (cls == 4) kb = UIKeyboardTypeNumbersAndPunctuation;
    else if (cls == 1) {                     // TYPE_CLASS_TEXT
        if (variation == 0x20 || variation == 0xD0) kb = UIKeyboardTypeEmailAddress;
        else if (variation == 0x10) kb = UIKeyboardTypeURL;
        secure = variation == 0x80 || variation == 0xE0;      // password, web password (not the visible one)
    }
    self.keyboardType = kb;
    self.secureTextEntry = secure;
    self.autocapitalizationType = cls != 1 ? UITextAutocapitalizationTypeNone
        : (flags & 0x1000) ? UITextAutocapitalizationTypeAllCharacters : (flags & 0x2000) ? UITextAutocapitalizationTypeWords
        : (flags & 0x4000) ? UITextAutocapitalizationTypeSentences : UITextAutocapitalizationTypeNone;
    // whole words only: the keyboard does not see the field's text, so it may not correct what is already typed
    self.autocorrectionType = UITextAutocorrectionTypeNo;
    self.spellCheckingType = UITextSpellCheckingTypeNo;
    BOOL multiLine = cls == 1 && (flags & 0x20000) && !(ime & 0x40000000);   // TYPE_TEXT_FLAG_MULTI_LINE, IME_FLAG_NO_ENTER_ACTION
    switch (multiLine ? 0 : ime & 0xFF) {
    case 2: self.returnKeyType = UIReturnKeyGo; break;
    case 3: self.returnKeyType = UIReturnKeySearch; break;
    case 4: self.returnKeyType = UIReturnKeySend; break;
    case 5: self.returnKeyType = UIReturnKeyNext; break;
    case 6: self.returnKeyType = UIReturnKeyDone; break;
    default: self.returnKeyType = UIReturnKeyDefault; break;
    }
}
@end

static KeyInput *gKeyInput;

static void ShowKeyboard(bool show, int type, int ime)
{
    dispatch_async(dispatch_get_main_queue(), ^{
        if (!gKeyInput) return;
        if (show) {
            [gKeyInput configureType:type options:ime];
            if (gKeyInput.isFirstResponder) [gKeyInput reloadInputViews];
            else [gKeyInput becomeFirstResponder];
        } else if (gKeyInput.isFirstResponder) {
            gKeyInput.hiding = YES;
            [gKeyInput resignFirstResponder];
        }
    });
}

static void KeyboardMoved(NSNotification *note)
{
    CGRect end = [note.userInfo[UIKeyboardFrameEndUserInfoKey] CGRectValue];
    UIScreen *screen = UIScreen.mainScreen;
    BOOL hide = [note.name isEqualToString:UIKeyboardWillHideNotification];
    CGFloat h = hide ? 0 : fmax(0, screen.bounds.size.height - end.origin.y);
    gKeyboard = (int)(h * screen.scale);
    SendInsets();
    if (hide) {
        if (gKeyInput && !gKeyInput.hiding) husk_app_text_hidden();   // the user closed it: the app hears so
        gKeyInput.hiding = NO;
    }
}

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
    gKeyInput = [[KeyInput alloc] initWithFrame:CGRectZero];
    [frames addSubview:gKeyInput];
    for (NSNotificationName n in @[ UIKeyboardWillChangeFrameNotification, UIKeyboardWillHideNotification ])
        [NSNotificationCenter.defaultCenter addObserverForName:n object:nil queue:NSOperationQueue.mainQueue usingBlock:^(NSNotification *note) { KeyboardMoved(note); }];
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
    gStats = getenv("HUSK_FRAME_STATS") != NULL;
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
        .night_mode = screen.traitCollection.userInterfaceStyle == UIUserInterfaceStyleDark,   /* the screen's: known before the window shows */
        .open_url = OpenURL,
        .set_clipboard = SetClipboard,
        .get_clipboard = GetClipboard,
        .show_keyboard = ShowKeyboard,
        .vsync = true,                      // nextFrame: each refresh of the display link
        .refresh_rate = (float)screen.maximumFramesPerSecond,
    };
    husk_app_configure(&cfg);
    UIEdgeInsets i = self.window.safeAreaInsets;
    gInsets[0] = (int)(i.left * scale); gInsets[1] = (int)(i.top * scale); gInsets[2] = (int)(i.right * scale); gInsets[3] = (int)(i.bottom * scale);
    SendInsets();

    // ART wants a big stack; the main thread's belongs to UIKit
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 64 << 20);
    // the app's UI thread: iOS holds its timers to time and schedules it as it does its own main thread
    pthread_attr_set_qos_class_np(&attr, QOS_CLASS_USER_INTERACTIVE, 0);
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
