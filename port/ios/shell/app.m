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
#import <Metal/Metal.h>
#import <QuartzCore/QuartzCore.h>
#import <UIKit/UIKit.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>
#import <AVFoundation/AVFoundation.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <unistd.h>

#include "husk-framework.h"
#include "husk-ios-jit.h"
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

// ---- the screen: the app's latest frame drawn by Metal from its own memory (or as the layer's contents); touches to it

/* Metal: a frame's buffer is wrapped as it is (whole pages, gfx.c) and read by a fragment shader pixel by pixel, the screen's
 * pixel to the frame's: no copy, no texture (a frame's rows, w * 4 bytes, are not as Metal aligns a texture's). The frame is
 * given back when the GPU is done with it. */
static id<MTLDevice> gDev;
static id<MTLCommandQueue> gQueue;
static id<MTLRenderPipelineState> gPipe;
static const char *kShader =
    "#include <metal_stdlib>\n"
    "using namespace metal;\n"
    "struct VO { float4 p [[position]]; };\n"
    "vertex VO frame_v(uint id [[vertex_id]]) {\n"
    "    const float2 c[3] = { float2(-1, -1), float2(3, -1), float2(-1, 3) };\n"
    "    VO o; o.p = float4(c[id], 0, 1); return o;\n"
    "}\n"
    "fragment half4 frame_f(VO in [[stage_in]], device const uchar4 *px [[buffer(0)]], constant uint2 &size [[buffer(1)]],\n"
    "                       constant float2 &scale [[buffer(2)]]) {\n"
    "    uint2 q = min(uint2(in.p.xy * scale), size - 1);\n"
    "    return half4(px[q.y * size.x + q.x]) / 255.0h;\n"   /* RGBA, premultiplied, as the drawable takes it */
    "}\n";
static bool MetalReady(void)
{
    static int ready = -1;
    if (ready >= 0) return ready;
    ready = 0;
    if (getenv("HUSK_PRESENT_CG")) return false;
    gDev = MTLCreateSystemDefaultDevice();
    if (!gDev) return false;
    NSError *e = nil;
    id<MTLLibrary> lib = [gDev newLibraryWithSource:@(kShader) options:nil error:&e];
    if (!lib) { fprintf(stderr, "husk-port: Metal: %s\n", e.localizedDescription.UTF8String); return false; }
    MTLRenderPipelineDescriptor *d = [MTLRenderPipelineDescriptor new];
    d.vertexFunction = [lib newFunctionWithName:@"frame_v"];
    d.fragmentFunction = [lib newFunctionWithName:@"frame_f"];
    d.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
    gPipe = [gDev newRenderPipelineStateWithDescriptor:d error:&e];
    if (!gPipe) { fprintf(stderr, "husk-port: Metal: %s\n", e.localizedDescription.UTF8String); return false; }
    gQueue = [gDev newCommandQueue];
    ready = gQueue != nil;
    if (ready) fprintf(stderr, "husk-port: frames shown through Metal (%s)\n", gDev.name.UTF8String);
    return ready;
}

@interface FrameView : UIView
@end

@implementation FrameView {
    CADisplayLink *_link;
    NSMapTable<UITouch *, NSNumber *> *_ids;
    int _nextId;
}

+ (Class)layerClass { return MetalReady() ? CAMetalLayer.class : CALayer.class; }

- (instancetype)initWithFrame:(CGRect)frame
{
    if ((self = [super initWithFrame:frame])) {
        self.multipleTouchEnabled = YES;
        self.backgroundColor = UIColor.blackColor;
        self.layer.contentsGravity = kCAGravityResize;
        if ([self.layer isKindOfClass:CAMetalLayer.class]) {
            CAMetalLayer *m = (CAMetalLayer *)self.layer;
            m.device = gDev;
            m.pixelFormat = MTLPixelFormatBGRA8Unorm;
            m.framebufferOnly = YES;
            m.maximumDrawableCount = 3;
            CGColorSpaceRef cs = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
            m.colorspace = cs;
            CGColorSpaceRelease(cs);
        }
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

static void ReleaseFrame(void *info, const void *data, size_t size) { (void)data; (void)size; husk_frame_release((int)(intptr_t)info); }

- (void)nextFrame:(CADisplayLink *)link
{
    husk_app_vsync();                       // the app's Choreographer draws its next frame now, when it wants one
    const uint8_t *px; int w, h; bool any;
    if (gStats) gStatTicks++;
    double t0 = gStats ? CACurrentMediaTime() : 0;
    int token;
    if (!husk_frame_hold(&px, &w, &h, &any, &token)) return;
    if ([self.layer isKindOfClass:CAMetalLayer.class]) { [self showMetal:px w:w h:h any:any token:token]; if (gStats) FrameStat(t0, CACurrentMediaTime()); return; }
    CGImageRef img = NULL;
    if (any && w > 0 && h > 0) {
        // the frame's own buffer, not a copy: given back when Core Animation lets go of the image
        CGDataProviderRef p = CGDataProviderCreateWithData((void *)(intptr_t)token, px, (size_t)w * h * 4, ReleaseFrame);
        CGColorSpaceRef cs = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
        img = CGImageCreate((size_t)w, (size_t)h, 8, 32, (size_t)w * 4, cs,
                            kCGImageAlphaPremultipliedLast | kCGBitmapByteOrder32Big, p, NULL, false, kCGRenderingIntentDefault);
        CGColorSpaceRelease(cs); CGDataProviderRelease(p);
        if (!img) husk_frame_release(token);
    } else husk_frame_release(token);
    [CATransaction begin];
    [CATransaction setDisableActions:YES];
    self.layer.contents = (__bridge id)img;
    [CATransaction commit];
    if (img) CGImageRelease(img);
    if (gStats) FrameStat(t0, CACurrentMediaTime());
}

- (void)layoutSubviews
{
    [super layoutSubviews];
    if ([self.layer isKindOfClass:CAMetalLayer.class]) {
        CGFloat k = self.window.screen.nativeScale ?: UIScreen.mainScreen.nativeScale;
        ((CAMetalLayer *)self.layer).drawableSize = CGSizeMake(self.bounds.size.width * k, self.bounds.size.height * k);
    }
}

- (void)showMetal:(const uint8_t *)px w:(int)w h:(int)h any:(bool)any token:(int)token
{
    CAMetalLayer *m = (CAMetalLayer *)self.layer;
    id<CAMetalDrawable> drawable = [m nextDrawable];
    if (!drawable) { husk_frame_release(token); return; }
    MTLRenderPassDescriptor *pass = [MTLRenderPassDescriptor renderPassDescriptor];
    pass.colorAttachments[0].texture = drawable.texture;
    pass.colorAttachments[0].loadAction = any ? MTLLoadActionDontCare : MTLLoadActionClear;
    pass.colorAttachments[0].clearColor = MTLClearColorMake(0, 0, 0, 1);
    pass.colorAttachments[0].storeAction = MTLStoreActionStore;
    id<MTLCommandBuffer> cmd = [gQueue commandBuffer];
    id<MTLRenderCommandEncoder> enc = [cmd renderCommandEncoderWithDescriptor:pass];
    if (any && w > 0 && h > 0) {
        size_t len = ((size_t)w * 4 * (size_t)h + 16383) & ~(size_t)16383;
        id<MTLBuffer> buf = [gDev newBufferWithBytesNoCopy:(void *)px length:len options:MTLResourceStorageModeShared deallocator:nil];
        if (buf) {
            uint32_t size[2] = { (uint32_t)w, (uint32_t)h };
            float scale[2] = { (float)w / (float)drawable.texture.width, (float)h / (float)drawable.texture.height };
            [enc setRenderPipelineState:gPipe];
            [enc setFragmentBuffer:buf offset:0 atIndex:0];
            [enc setFragmentBytes:size length:sizeof size atIndex:1];
            [enc setFragmentBytes:scale length:sizeof scale atIndex:2];
            [enc drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
        }
    }
    [enc endEncoding];
    [cmd presentDrawable:drawable];
    [cmd addCompletedHandler:^(id<MTLCommandBuffer> b) { (void)b; husk_frame_release(token); }];
    [cmd commit];
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

// ---- the system's file picker (husk.Documents: GET_CONTENT, OPEN_DOCUMENT, PICK): what the user picks is copied where the
// app can read it by path long after the picker's access ends

/* the document types for an intent's MIME types ("image/*,application/pdf"); anything unknown opens it to all files */
static NSArray<UTType *> *ContentTypes(NSString *mimes)
{
    NSMutableArray<UTType *> *types = [NSMutableArray array];
    for (NSString *raw in [mimes componentsSeparatedByString:@","]) {
        NSString *m = [raw stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceCharacterSet].lowercaseString;
        UTType *t = nil;
        if (m.length == 0 || [m isEqualToString:@"*/*"]) return @[ UTTypeItem ];
        else if ([m isEqualToString:@"image/*"]) t = UTTypeImage;
        else if ([m isEqualToString:@"video/*"]) t = UTTypeMovie;
        else if ([m isEqualToString:@"audio/*"]) t = UTTypeAudio;
        else if ([m isEqualToString:@"text/*"]) t = UTTypeText;
        else if ([m isEqualToString:@"application/*"]) t = UTTypeData;
        else t = [UTType typeWithMIMEType:m];
        if (!t) return @[ UTTypeItem ];
        [types addObject:t];
    }
    return types.count ? types : @[ UTTypeItem ];
}
static void Picked(NSArray<NSString *> *paths)
{
    const char *c[paths.count ? paths.count : 1];
    for (NSUInteger i = 0; i < paths.count; i++) c[i] = paths[i].UTF8String;
    husk_app_documents_picked(c, (int)paths.count);
}

@interface PickerAnswer : NSObject <UIDocumentPickerDelegate>
@end
static PickerAnswer *gPicker;           /* the picker up, if any: its delegate is weak */
@implementation PickerAnswer
- (void)documentPicker:(UIDocumentPickerViewController *)controller didPickDocumentsAtURLs:(NSArray<NSURL *> *)urls
{
    gPicker = nil;
    NSFileManager *fm = NSFileManager.defaultManager;
    NSURL *dir = [NSFileManager.defaultManager.temporaryDirectory URLByAppendingPathComponent:[@"picked" stringByAppendingPathComponent:NSUUID.UUID.UUIDString] isDirectory:YES];
    [fm createDirectoryAtURL:dir withIntermediateDirectories:YES attributes:nil error:nil];
    NSMutableArray<NSString *> *paths = [NSMutableArray array];
    for (NSURL *u in urls) {
        BOOL scoped = [u startAccessingSecurityScopedResource];
        NSURL *dest = [dir URLByAppendingPathComponent:u.lastPathComponent];
        NSError *e = nil;
        if ([fm copyItemAtURL:u toURL:dest error:&e]) [paths addObject:dest.path];
        else NSLog(@"husk-port: could not copy the picked %@: %@", u.lastPathComponent, e.localizedDescription);
        if (scoped) [u stopAccessingSecurityScopedResource];
    }
    Picked(paths);
}
- (void)documentPickerWasCancelled:(UIDocumentPickerViewController *)controller { gPicker = nil; Picked(@[]); }
@end

static void PickDocuments(const char *mime_types, bool multiple)
{
    NSString *mimes = @(mime_types);
    dispatch_async(dispatch_get_main_queue(), ^{
        if (gPicker) { Picked(@[]); return; }
        UIViewController *top = UIApplication.sharedApplication.keyWindow.rootViewController;
        while (top.presentedViewController && !top.presentedViewController.isBeingDismissed) top = top.presentedViewController;
        if (!top) { Picked(@[]); return; }
        UIDocumentPickerViewController *p = [[UIDocumentPickerViewController alloc] initForOpeningContentTypes:ContentTypes(mimes) asCopy:NO];
        p.allowsMultipleSelection = multiple;
        gPicker = [PickerAnswer new];
        p.delegate = gPicker;
        [top presentViewController:p animated:YES completion:nil];
    });
}

/* the battery as BatteryManager has it: level | status << 8 (2 charging, 3 discharging, 5 full) | plugged << 16 (2 USB) */
static int Battery(void)
{
    __block int r = -1;
    void (^get)(void) = ^{
        UIDevice *d = UIDevice.currentDevice;
        d.batteryMonitoringEnabled = YES;
        float lvl = d.batteryLevel;
        if (lvl < 0) return;
        UIDeviceBatteryState s = d.batteryState;
        int status = s == UIDeviceBatteryStateCharging ? 2 : s == UIDeviceBatteryStateFull ? 5 : 3;
        int plugged = s == UIDeviceBatteryStateCharging || s == UIDeviceBatteryStateFull ? 2 : 0;
        r = (int)lroundf(lvl * 100) | status << 8 | plugged << 16;
    };
    if (NSThread.isMainThread) get(); else dispatch_sync(dispatch_get_main_queue(), get);
    return r;
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
        .pick_documents = PickDocuments,
        .battery = Battery,
        .native_dir = NSBundle.mainBundle.privateFrameworksPath.UTF8String,   // aot-*.dylib: the app's libraries, made ahead of time
        .vsync = true,                      // nextFrame: each refresh of the display link
        .refresh_rate = (float)screen.maximumFramesPerSecond,
    };
    husk_app_configure(&cfg);
    // sound as an Android app's media: heard with the ring/silent switch on silent, alongside other apps' music
    AVAudioSession *session = AVAudioSession.sharedInstance;
    [session setCategory:AVAudioSessionCategoryPlayback withOptions:AVAudioSessionCategoryOptionMixWithOthers error:nil];
    [session setActive:YES error:nil];
    // a call or an alarm stops the output; it starts again when they are over
    [NSNotificationCenter.defaultCenter addObserverForName:AVAudioSessionInterruptionNotification object:session queue:NSOperationQueue.mainQueue
                                                usingBlock:^(NSNotification *n) {
        bool began = [n.userInfo[AVAudioSessionInterruptionTypeKey] unsignedIntegerValue] == AVAudioSessionInterruptionTypeBegan;
        if (!began) [AVAudioSession.sharedInstance setActive:YES error:nil];
        husk_app_audio_paused(began);
    }];
    UIEdgeInsets i = self.window.safeAreaInsets;
    gInsets[0] = (int)(i.left * scale); gInsets[1] = (int)(i.top * scale); gInsets[2] = (int)(i.right * scale); gInsets[3] = (int)(i.bottom * scale);
    SendInsets();

    // executable memory for the app's own native libraries: StikDebug's region (through LocalDevVPN), or the process's own where
    // JIT is enabled; without either, the libraries that need it do not load and their methods return nothing
    husk_ios_jit_install_trap_handler();
    if (husk_ios_jit_prewarm(768u << 20)) husk_ios_jit_detach();

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
// off screen (the app switcher, the lock screen, Control Center): no sound until it is back
- (void)applicationWillResignActive:(UIApplication *)app { husk_app_audio_paused(true); }
- (void)applicationDidBecomeActive:(UIApplication *)app { husk_app_audio_paused(false); }
@end

int main(int argc, char *argv[])
{
    @autoreleasepool {
        return UIApplicationMain(argc, argv, nil, NSStringFromClass(AppDelegate.class));
    }
}
