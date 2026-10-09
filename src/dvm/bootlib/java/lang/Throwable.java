package java.lang;

public class Throwable implements java.io.Serializable {
    private String detailMessage;
    private Throwable cause = this;
    private transient Object backtrace;
    private Throwable[] suppressed;

    public Throwable() { fillInStackTrace(); }
    public Throwable(String message) { fillInStackTrace(); detailMessage = message; }
    public Throwable(String message, Throwable cause) { fillInStackTrace(); detailMessage = message; this.cause = cause; }
    public Throwable(Throwable cause) { fillInStackTrace(); detailMessage = cause == null ? null : cause.toString(); this.cause = cause; }

    public Throwable fillInStackTrace() { nativeFillInStackTrace(); return this; }
    private native void nativeFillInStackTrace();
    private static native String[] nativeGetStackTrace(Object backtrace);

    public String getMessage() { return detailMessage; }
    public String getLocalizedMessage() { return getMessage(); }
    public Throwable getCause() { return cause == this ? null : cause; }
    public Throwable initCause(Throwable c) {
        if (cause != this) throw new IllegalStateException("Can't overwrite cause");
        if (c == this) throw new IllegalArgumentException("Self-causation not permitted");
        cause = c;
        return this;
    }
    public final void addSuppressed(Throwable t) {
        if (t == this) throw new IllegalArgumentException("Self-suppression not permitted");
        if (t == null) throw new NullPointerException("Cannot suppress a null exception.");
        Throwable[] s = suppressed == null ? new Throwable[1] : java.util.Arrays.copyOf(suppressed, suppressed.length + 1);
        s[s.length - 1] = t;
        suppressed = s;
    }
    public final Throwable[] getSuppressed() { return suppressed == null ? new Throwable[0] : suppressed.clone(); }
    public String[] getStackTraceStrings() { return nativeGetStackTrace(backtrace); }
    public String toString() {
        String m = getLocalizedMessage();
        return m != null ? getClass().getName() + ": " + m : getClass().getName();
    }
    public void printStackTrace() { printStackTrace(System.err); }
    public void printStackTrace(java.io.PrintStream s) {
        s.println(this);
        for (String f : getStackTraceStrings()) s.println("\tat " + f);
        for (Throwable t : getSuppressed()) { s.print("\tSuppressed: "); t.printStackTrace(s); }
        Throwable c = getCause();
        if (c != null) { s.print("Caused by: "); c.printStackTrace(s); }
    }
}
