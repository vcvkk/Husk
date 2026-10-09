# husk-port

Turn an Android app into an iPhone app: give it an APK, get back an `.ipa`
that contains that one app and the runtime it needs, and nothing else.

husk-port grows out of [Husk](https://github.com/Leviidev/Husk)'s translation
layer, which already runs the native code of Android games directly on the
iPhone's CPU (Unity, Unreal Engine 4, Godot, cocos2d-x, SDL, GameActivity,
NativeActivity). Husk is a launcher for many APKs; husk-port is a porting
tool that produces one IPA per APK.

**Status: early.** Nothing here produces an IPA yet. What exists and is
tested is the start of the part Husk never had: a Dalvik VM, so that apps
written in Java or Kotlin can run too, not only games whose logic is native.

## Why a VM

Husk's translation layer does not run Java at all. A game's Java is a thin
shell around its engine, so the layer answers the engine's JNI calls with
hand-written C (`src/translation-layer-next/husk-tl-jni-*.c`). An ordinary
app is the other way round: almost all of it is DEX bytecode on top of the
Android framework. Running those needs a real VM, then Java's own class
library (libcore), then the framework. See [docs/port-plan.md](docs/port-plan.md).

## Layout

| Path | What |
|---|---|
| `src/dvm/` | The Dalvik VM: DEX loading, classes and vtables, the interpreter, exceptions, a mark-sweep heap |
| `src/dvm/bootlib/` | A small Java class library the VM boots with until AOSP's libcore runs on it |
| `tools/dvm-run.c` | Runs a DEX program on a computer: `dvm-run -cp boot.dex:app.dex Main` |
| `tests/dvm/` | Java programs run on the host JVM and on the DVM; the outputs must match |
| `src/translation-layer-next/` | Husk's native runtime: ELF loader, bionic on Darwin, engine drivers, JNI |
| `src/translation-layer/` | Husk's APK scanner and its first DEX interpreter, which the DVM replaces |
| `src/app/` | Husk's iOS app, the basis of the per-app runtime |

The QEMU emulation side of Husk (`src/ios-jit`, `patches/`, the guest image
scripts) is still here because the iOS app builds against it; it goes when
the per-app runtime is split out of the app.

## Tests

```
tests/dvm/run.sh                 # all tests
tests/dvm/run.sh Exceptions      # one
DVM_GC_STRESS=7 tests/dvm/run.sh # collect every 7 allocations
DVM_SANITIZE=1 tests/dvm/run.sh  # ASan and UBSan
```

Needs clang, zlib, a JDK (`javac`, `java`) and network access the first time,
to fetch d8. Runs on Linux and macOS.

## Licence

GPL-2.0-or-later, as Husk. See [LICENSE](LICENSE) and
[docs/01-licensing.md](docs/01-licensing.md).
