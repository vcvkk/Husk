# husk-port: the plan

An APK goes in; an `.ipa` holding that app and the runtime it needs comes out.
What it takes depends on what the app is made of, so the work is in stages,
each useful on its own.

## Stage 1: one IPA per game, on Husk's native runtime

For apps Husk's translation layer already runs (a known engine, 64-bit native
code, little Java):

- scan the APK (`src/translation-layer/husk-tl-scan.c`, `ApkMetadata.swift`):
  ABI, engine, 16 KiB pages, direct system calls;
- take a runtime app: Husk without QEMU, the library, the Play store and
  downloads; it starts the one app it carries;
- put the APK in the bundle, with a configuration (engine, orientation,
  settings), and an `Info.plist` with the app's name, bundle identifier and
  icon from its manifest;
- sign ad hoc, or leave it to the sideloader.

Without QEMU in the process, the licensing problem in
[04-translation-layer.md](04-translation-layer.md) goes away.

## Stage 2: native code without JIT

Today every launch needs JIT (StikDebug, TrollStore or the built-in StikJIT),
only because `.so` files are loaded into executable memory at run time. A tool
on a computer can do the loader's work ahead of time: relocate, rewrite
`TPIDR_EL0`, `svc` and x18 uses, lay segments out on 16 KiB pages, and write a
Mach-O dylib whose imports bind to the bionic shim. The IPA then signs and runs
like any other app. Code that generates code at run time (Mono's JIT, LuaJIT,
V8) still needs JIT; IL2CPP and UE4 games do not.

## Stage 3: apps written in Java and Kotlin

1. **The VM core** (`src/dvm/`, in progress): DEX loading, vtable and
   interface dispatch, exceptions with try/catch tables, class initialisation,
   a garbage collector. Tested against the host JVM by `tests/dvm/run.sh`.
   Next: threads and monitors, `invoke-custom`/`invoke-polymorphic`, JNI.
2. **libcore**: run AOSP's libcore bytecode (`java.lang`, `java.util`, ...) on
   the VM instead of `src/dvm/bootlib`, implementing its native methods in C.
   Milestone: `HashMap` and `String.format` from libcore work.
3. **One Java world**: the JNI implementation in
   `src/translation-layer-next/husk-tl-jni.c` keeps its own objects (`jobj`).
   Native code in hybrid apps must reach the app's real bytecode, so JNI moves
   onto the DVM's objects.
4. **The framework**: AOSP's `framework.jar` (Apache-2.0) or ATL's `api-impl`
   (GPL-3.0; choosing it makes the whole project GPL-3.0), with its native half
   written for UIKit, Core Graphics/Metal and Core Text. Milestone: an Activity
   with a `TextView` and a button on an iPhone screen.
5. **Speed**: the interpreter is a `switch` loop. The same ahead-of-time idea
   as stage 2 applies to DEX: compile it to arm64 on the computer and ship it
   in the IPA.

Nothing beyond stage 3.1 should be described as working until it has been
seen working.
