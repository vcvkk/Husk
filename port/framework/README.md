# The Android framework apps run on

`android.*` (Activity, View, resources, a Canvas over CoreGraphics and CoreText,
WebView, SQLite, GLES...) and `husk.*` (the app driver), in Java: Husk's main
line's framework (Leviidev/Husk `src/java-framework` at 9db95ed), on which Jetpack
Compose runs. There its natives are implemented inside Husk's own Dalvik
interpreter; here it runs on ART, in the boot image, and its natives are JNI
(`jni/`).

    R8_JAR=... port/framework/build.sh framework.jar
