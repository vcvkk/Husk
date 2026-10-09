package java.io;

public class PrintStream implements Closeable {
    private final int fd;
    public PrintStream(int fd) { this.fd = fd; }
    private static native void writeString(int fd, String s);
    public void print(String s) { writeString(fd, s == null ? "null" : s); }
    public void print(Object o) { print(String.valueOf(o)); }
    public void print(char c) { print(String.valueOf(c)); }
    public void print(char[] c) { print(new String(c)); }
    public void print(int i) { print(String.valueOf(i)); }
    public void print(long l) { print(String.valueOf(l)); }
    public void print(float f) { print(String.valueOf(f)); }
    public void print(double d) { print(String.valueOf(d)); }
    public void print(boolean b) { print(String.valueOf(b)); }
    public void println() { print("\n"); }
    public void println(String s) { print((s == null ? "null" : s) + "\n"); }
    public void println(Object o) { println(String.valueOf(o)); }
    public void println(char c) { println(String.valueOf(c)); }
    public void println(char[] c) { println(new String(c)); }
    public void println(int i) { println(String.valueOf(i)); }
    public void println(long l) { println(String.valueOf(l)); }
    public void println(float f) { println(String.valueOf(f)); }
    public void println(double d) { println(String.valueOf(d)); }
    public void println(boolean b) { println(String.valueOf(b)); }
    public void flush() {}
    public void close() {}
}
