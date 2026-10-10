// SPDX-License-Identifier: GPL-2.0-or-later
// The app that runs an Android program on ART, stage 1: a window of text, and a Java VM
// started in it that runs the program's main(String[]). What the program prints shows up
// on screen (and in the device log).
//
// The bundle (port/ios/pack-ipa.sh):
//   <exe>, Frameworks/lib*.dylib     ART's runtime, the class library's natives
//   art/boot.classpath, art/jars/    the boot class path, as compiled
//   art/arm64/boot*.{art,oat,vdex}   the boot image; boot*.oat.dylib its code, signed
//   app/classes.dex                  the program; app/oat/arm64/classes.odex{,.dylib} its code
//   Info.plist HuskMainClass         the class whose main() runs
//
// Nothing is compiled or generated at run time: ART maps each oat file's code from its
// signed dylib (patch 0008) and runs without JIT.
#import <UIKit/UIKit.h>
#include <jni.h>
#include <pthread.h>
#include <stdio.h>
#include <unistd.h>

static UITextView *gText;

static void Show(NSString *s) {
  dispatch_async(dispatch_get_main_queue(), ^{
    gText.text = [gText.text stringByAppendingString:s];
    [gText scrollRangeToVisible:NSMakeRange(gText.text.length, 0)];
  });
}

// stdout and stderr into a pipe whose reader puts them on screen, and on the original stderr
static int gErr = STDERR_FILENO;

static void *Pump(void *arg) {
  int fd = (int)(intptr_t)arg, err = gErr;
  char buf[4096];
  for (ssize_t n; (n = read(fd, buf, sizeof buf)) > 0;) {
    write(err, buf, n);
    NSString *s = [[NSString alloc] initWithBytes:buf length:n encoding:NSUTF8StringEncoding];
    if (s) Show(s);
  }
  return NULL;
}

static void Capture(void) {
  int p[2];
  if (pipe(p) != 0) return;
  setvbuf(stdout, NULL, _IOLBF, 0);
  gErr = dup(STDERR_FILENO);
  pthread_t t;
  pthread_create(&t, NULL, Pump, (void *)(intptr_t)p[0]);
  dup2(p[1], STDOUT_FILENO);
  dup2(p[1], STDERR_FILENO);
}

static NSString *Joined(NSString *art, NSString *list, BOOL paths) {
  NSMutableArray *out = [NSMutableArray array];
  for (NSString *loc in [list componentsSeparatedByString:@":"])
    [out addObject:paths ? [art stringByAppendingPathComponent:[@"jars" stringByAppendingPathComponent:loc.lastPathComponent]] : loc];
  return [out componentsJoinedByString:@":"];
}

static void *RunVM(void *unused) {
  NSString *bundle = NSBundle.mainBundle.bundlePath;
  NSString *art = [bundle stringByAppendingPathComponent:@"art"];
  NSString *dex = [bundle stringByAppendingPathComponent:@"app/classes.dex"];
  NSString *main = [NSBundle.mainBundle objectForInfoDictionaryKey:@"HuskMainClass"] ?: @"Hello";
  NSString *bcp = [[NSString stringWithContentsOfFile:[art stringByAppendingPathComponent:@"boot.classpath"]
                                             encoding:NSUTF8StringEncoding error:nil]
                      stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceAndNewlineCharacterSet];
  NSString *data = [NSTemporaryDirectory() stringByAppendingPathComponent:@"android-data"];
  [NSFileManager.defaultManager createDirectoryAtPath:[data stringByAppendingPathComponent:@"dalvik-cache"]
                          withIntermediateDirectories:YES attributes:nil error:nil];
  setenv("ANDROID_DATA", data.UTF8String, 1);
  setenv("ANDROID_ROOT", bundle.UTF8String, 1);

  NSArray *opts = @[
    @"-Xusejit:false",
    [@"-Ximage:" stringByAppendingString:[art stringByAppendingPathComponent:@"boot.art"]],
    [@"-Xbootclasspath:" stringByAppendingString:Joined(art, bcp, YES)],
    [@"-Xbootclasspath-locations:" stringByAppendingString:bcp],
    [@"-Djava.class.path=" stringByAppendingString:dex],
    @"-Xss8m",
  ];
  JavaVMOption options[opts.count];
  for (NSUInteger i = 0; i < opts.count; i++) options[i] = (JavaVMOption){ .optionString = (char *)[opts[i] UTF8String] };
  JavaVMInitArgs args = { .version = JNI_VERSION_1_6, .nOptions = (jint)opts.count, .options = options };
  JavaVM *vm; JNIEnv *env;
  printf("husk-port: starting ART for %s\n", main.UTF8String);
  if (JNI_CreateJavaVM(&vm, &env, &args) != JNI_OK) {
    printf("husk-port: JNI_CreateJavaVM failed\n");
    return NULL;
  }
  // the program's class, through the system class loader (java.class.path)
  jclass loaderClass = (*env)->FindClass(env, "java/lang/ClassLoader");
  jobject loader = (*env)->CallStaticObjectMethod(env, loaderClass,
      (*env)->GetStaticMethodID(env, loaderClass, "getSystemClassLoader", "()Ljava/lang/ClassLoader;"));
  jstring name = (*env)->NewStringUTF(env, main.UTF8String);
  jclass cls = (*env)->CallObjectMethod(env, loader,
      (*env)->GetMethodID(env, loaderClass, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;"), name);
  if ((*env)->ExceptionCheck(env)) { (*env)->ExceptionDescribe(env); return NULL; }
  jmethodID m = (*env)->GetStaticMethodID(env, cls, "main", "([Ljava/lang/String;)V");
  jobjectArray argv = (*env)->NewObjectArray(env, 0, (*env)->FindClass(env, "java/lang/String"), NULL);
  (*env)->CallStaticVoidMethod(env, cls, m, argv);
  if ((*env)->ExceptionCheck(env)) (*env)->ExceptionDescribe(env);
  printf("husk-port: %s.main returned\n", main.UTF8String);
  fflush(stdout);
  return NULL;
}

@interface AppDelegate : UIResponder <UIApplicationDelegate>
@property(strong, nonatomic) UIWindow *window;
@end

@implementation AppDelegate
- (BOOL)application:(UIApplication *)app didFinishLaunchingWithOptions:(NSDictionary *)opts {
  self.window = [[UIWindow alloc] initWithFrame:UIScreen.mainScreen.bounds];
  UIViewController *vc = [UIViewController new];
  gText = [[UITextView alloc] initWithFrame:vc.view.bounds];
  gText.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
  gText.editable = NO;
  gText.font = [UIFont monospacedSystemFontOfSize:12 weight:UIFontWeightRegular];
  gText.text = @"";
  [vc.view addSubview:gText];
  self.window.rootViewController = vc;
  [self.window makeKeyAndVisible];

  Capture();
  // ART wants a big stack; the main thread's belongs to UIKit
  pthread_attr_t attr;
  pthread_attr_init(&attr);
  pthread_attr_setstacksize(&attr, 16 << 20);
  pthread_t t;
  pthread_create(&t, &attr, RunVM, NULL);
  return YES;
}
@end

int main(int argc, char *argv[]) {
  @autoreleasepool {
    return UIApplicationMain(argc, argv, nil, NSStringFromClass(AppDelegate.class));
  }
}
