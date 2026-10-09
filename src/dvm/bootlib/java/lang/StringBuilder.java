package java.lang;

public final class StringBuilder implements CharSequence, java.io.Serializable {
    private char[] buf;
    private int len;

    public StringBuilder() { buf = new char[16]; }
    public StringBuilder(int cap) { buf = new char[cap]; }
    public StringBuilder(String s) { buf = new char[s.length() + 16]; append(s); }
    public StringBuilder(CharSequence s) { this(s.toString()); }

    private void grow(int need) {
        if (len + need <= buf.length) return;
        int cap = Math.max(buf.length * 2 + 2, len + need);
        buf = java.util.Arrays.copyOf(buf, cap);
    }
    public StringBuilder append(String s) {
        if (s == null) s = "null";
        int n = s.length();
        grow(n);
        s.getChars(0, n, buf, len);
        len += n;
        return this;
    }
    public StringBuilder append(char c) { grow(1); buf[len++] = c; return this; }
    public StringBuilder append(Object o) { return append(String.valueOf(o)); }
    public StringBuilder append(CharSequence s) { return append(String.valueOf(s)); }
    public StringBuilder append(char[] c) { grow(c.length); System.arraycopy(c, 0, buf, len, c.length); len += c.length; return this; }
    public StringBuilder append(boolean b) { return append(b ? "true" : "false"); }
    public StringBuilder append(int i) { return append(Integer.toString(i)); }
    public StringBuilder append(long l) { return append(Long.toString(l)); }
    public StringBuilder append(float f) { return append(Float.toString(f)); }
    public StringBuilder append(double d) { return append(Double.toString(d)); }
    public StringBuilder insert(int at, String s) {
        if (at < 0 || at > len) throw new StringIndexOutOfBoundsException(at);
        int n = s.length();
        grow(n);
        System.arraycopy(buf, at, buf, at + n, len - at);
        s.getChars(0, n, buf, at);
        len += n;
        return this;
    }
    public StringBuilder reverse() {
        for (int i = 0, j = len - 1; i < j; i++, j--) { char t = buf[i]; buf[i] = buf[j]; buf[j] = t; }
        return this;
    }
    public StringBuilder deleteCharAt(int i) {
        if (i < 0 || i >= len) throw new StringIndexOutOfBoundsException(i);
        System.arraycopy(buf, i + 1, buf, i, len - i - 1);
        len--;
        return this;
    }
    public void setLength(int n) { grow(Math.max(0, n - len)); for (int i = len; i < n; i++) buf[i] = 0; len = n; }
    public void setCharAt(int i, char c) { if (i < 0 || i >= len) throw new StringIndexOutOfBoundsException(i); buf[i] = c; }
    public int length() { return len; }
    public char charAt(int i) { if (i < 0 || i >= len) throw new StringIndexOutOfBoundsException(i); return buf[i]; }
    public CharSequence subSequence(int b, int e) { return toString().substring(b, e); }
    public String toString() { return new String(buf, 0, len); }
}
