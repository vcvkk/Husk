package husk;

/**
 * An app's own native libraries (lib/arm64-v8a/ in its APK), for System.loadLibrary (libcore's Runtime.loadLibrary0 asks here
 * on Darwin, where it finds no file to load): through the guest linker (port/native), which maps one and binds it to its bionic,
 * or imports the dylib made from it ahead of time. ART then finds the library's Java_... functions and RegisterNatives'.
 */
public final class NativeLibs {
    private NativeLibs() {}
    private static native boolean nativeLoad(String file, ClassLoader loader);
    /** loader: the loading class's, which JNI_OnLoad's FindClass looks in (as Runtime.nativeLoad has it on Android) */
    public static synchronized boolean load(String name, ClassLoader loader) {
        try { return nativeLoad("lib" + name + ".so", loader); }
        catch (UnsatisfiedLinkError e) { return false; }   // a framework without the natives (an older host)
    }
    public static boolean load(String name) { return load(name, NativeLibs.class.getClassLoader()); }
}
