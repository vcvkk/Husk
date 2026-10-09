package java.lang;

public class Object {
    public Object() {}
    public final native Class<?> getClass();
    public native int hashCode();
    public boolean equals(Object o) { return this == o; }
    protected Object clone() throws CloneNotSupportedException { return internalClone(); }
    private native Object internalClone();
    public String toString() { return getClass().getName() + "@" + Integer.toHexString(hashCode()); }
    public final native void notify();
    public final native void notifyAll();
    public final void wait() throws InterruptedException {}
    public final void wait(long ms) throws InterruptedException {}
    protected void finalize() throws Throwable {}
}
