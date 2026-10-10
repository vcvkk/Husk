# husk-port: the plan

An APK goes in; an `.ipa` holding that app and the runtime it needs comes out,
for any app: games with a native engine and ordinary apps written in Java or
Kotlin alike. Without JIT if at all possible.

## The decision: AOSP's own runtime, compiled ahead of time

An ordinary app today means Kotlin on Jetpack Compose (the first real target,
`com.depcalc.mobile`, is 46,000 classes of it). Every frame runs bytecode:
recomposition, layout, drawing through `android.graphics`, text through
`android.text`. To do that at 60 fps:

- **ART, not a VM of our own.** libcore's and the framework's native methods
  are written against ART's internals (`dalvik.system.VMRuntime`,
  `sun.misc.Unsafe`, reflection, JNI, the GC's reference processing). Running
  the real libcore and framework means running ART. The starting point is ATL's
  `art_standalone`, which already builds ART (Android 10, oat version 170)
  outside AOSP's build system. `src/dvm` stays as a small test-bed; it is not
  the runtime.
- **All bytecode compiled to arm64 on the computer, at port time.** `dex2oat`
  runs on Linux or macOS and compiles the boot class path and the app.
- **Compiled code shipped as signed Mach-O dylibs, so no JIT.** ART already
  loads compiled code with `dlopen` and finds it by the symbols `oatdata`,
  `oatexec`, `oatlastword` and `oatbss` (`art/runtime/oat_file.cc`,
  `DlOpenOatFile`). A converter turns each ELF oat file into a Mach-O dylib
  exporting the same symbols; it is signed with the IPA, and iOS lets an app
  `dlopen` its own signed dylibs. ART runs with `-Xusejit:false`; anything not
  compiled runs in ART's interpreter, which is ordinary code in `libart`.
- **Native `.so` files the same way**: relocated, patched and converted to
  Mach-O ahead of time (Husk's loader logic, run at port time), with imports
  bound to the bionic shim.
- **JIT remains a fallback**, only for apps that generate machine code
  themselves (Mono's JIT, LuaJIT, V8): Husk's StikJIT path.

## Where it is developed: macOS on Apple Silicon first

An Apple Silicon Mac is the same CPU, the same Darwin kernel and libc, the same
16 KiB pages, dyld and code signing as an iPhone, and it runs headless on a
GitHub Actions runner (`macos-14` and later). So the runtime is ported to
`arm64-apple-darwin` first, where an APK can be started, traced and timed by
CI, and iOS is then a small step (UIKit instead of a window, the IPA's
bundle). Linux x86_64 is used for what does not depend on the target:
building `dex2oat`, compiling APKs, converting oat files.

## Where it stands

- **Toolchain: working.** `port/art/build.sh` builds ART (dex2oat, dalvikvm,
  libcore) with five patches (`port/art/patches`): oat files laid out on
  16 KiB pages, an arm64 back end in dex2oat on x86_64 hosts, and three fixes.
  `port/compile-apk.sh` compiles an APK, libcore and the framework to arm64
  and converts every oat file to an iOS dylib with `tools/oat2macho`.
  For `com.depcalc.mobile` that takes about 30 s and gives 94 MB of arm64 code
  (filter `speed`; a profile would cut it down).
- **The app on Linux: its first screens work.** On ATL (ART + `api-impl` with
  its GTK half) under Xvfb, the app starts, draws its Compose UI and goes
  through its onboarding on clicks. Found on the way: libcore must be desugared
  (d8 without `--no-desugaring`; ART 10 cannot link lambda call sites), and
  wolfSSL must be built with JNI.
- **Not started: ART on Darwin, the framework's native half on UIKit/Metal.**

## Stages

1. **Toolchain**: `dex2oat` built with the arm64 back end (VIXL), compiling
   the boot class path and an APK for arm64 on Linux. Then `oat2macho`.
2. **ART on Darwin arm64**: `art_standalone` built for macOS (16 KiB pages;
   `memfd_create`, `/proc`, `prctl`, futexes and bionic TLS replaced), running
   a `main()` from AOT-compiled code without JIT. Test: `tests/dvm` programs.
3. **The framework**: ATL's `api-impl` (GPL-3.0: husk-port then ships as
   GPL-3.0, which Husk's GPL-2.0-or-later code allows once QEMU is gone; ART
   and AOSP code are Apache-2.0, compatible with GPL-3.0). Its native half,
   written for GTK, is rewritten: windows and input on AppKit/UIKit, drawing on
   Skia with its Metal back end, text on HarfBuzz and ICU. Milestone: an app's
   first Activity draws its first frame into an image on the macOS runner.
4. **Compose at speed**: vsync from `CADisplayLink`, frame timing measured on
   the runner, then on an iPhone.
5. **iOS packaging**: the runtime as an app, the APK, its dylibs and the boot
   image in the bundle, an `Info.plist` from the manifest; signed or left to
   the sideloader.

Some Android features have no iOS counterpart and are dropped with a note in
the port report: overlays over other apps, quick-settings tiles, home-screen
widgets (iOS widgets are a separate SwiftUI extension), FCM/UnifiedPush (iOS
needs APNs), background services beyond what iOS allows.

Nothing should be described as working until it has been seen working.
