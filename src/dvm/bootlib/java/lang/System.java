package java.lang;

public final class System {
    public static final java.io.PrintStream out = new java.io.PrintStream(1);
    public static final java.io.PrintStream err = new java.io.PrintStream(2);
    private System() {}
    public static native void arraycopy(Object src, int srcPos, Object dst, int dstPos, int length);
    public static native long nanoTime();
    public static native long currentTimeMillis();
    public static native int identityHashCode(Object o);
    public static native void gc();
    public static native void exit(int status);
    public static String lineSeparator() { return "\n"; }
    public static String getProperty(String key) { return null; }
    public static String getProperty(String key, String def) { return def; }
}
