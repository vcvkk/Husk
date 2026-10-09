package java.lang;

public final class String implements java.io.Serializable, Comparable<String>, CharSequence {
    private final char[] value;
    private int hash;

    public String() { value = new char[0]; }
    public String(char[] v) { value = java.util.Arrays.copyOf(v, v.length); }
    public String(char[] v, int offset, int count) {
        if (offset < 0 || count < 0 || offset > v.length - count) throw new StringIndexOutOfBoundsException("offset " + offset + ", count " + count + ", length " + v.length);
        value = new char[count];
        System.arraycopy(v, offset, value, 0, count);
    }
    public String(String s) { value = s.value; hash = s.hash; }
    String(int unused, char[] shared) { value = shared; }

    public int length() { return value.length; }
    public boolean isEmpty() { return value.length == 0; }
    public char charAt(int i) {
        if (i < 0 || i >= value.length) throw new StringIndexOutOfBoundsException("index=" + i + " length=" + value.length);
        return value[i];
    }
    public char[] toCharArray() { return java.util.Arrays.copyOf(value, value.length); }
    public void getChars(int b, int e, char[] dst, int at) { System.arraycopy(value, b, dst, at, e - b); }

    public boolean equals(Object o) {
        if (this == o) return true;
        if (!(o instanceof String)) return false;
        char[] w = ((String) o).value;
        if (w.length != value.length) return false;
        for (int i = 0; i < w.length; i++) if (w[i] != value[i]) return false;
        return true;
    }
    public boolean equalsIgnoreCase(String s) {
        if (s == null || s.length() != length()) return false;
        for (int i = 0; i < value.length; i++)
            if (Character.toLowerCase(value[i]) != Character.toLowerCase(s.value[i])) return false;
        return true;
    }
    public int hashCode() {
        int h = hash;
        if (h == 0) {
            for (char c : value) h = 31 * h + c;
            hash = h;
        }
        return h;
    }
    public int compareTo(String s) {
        int n = Math.min(value.length, s.value.length);
        for (int i = 0; i < n; i++) if (value[i] != s.value[i]) return value[i] - s.value[i];
        return value.length - s.value.length;
    }

    public int indexOf(int c) { return indexOf(c, 0); }
    public int indexOf(int c, int from) {
        for (int i = Math.max(from, 0); i < value.length; i++) if (value[i] == c) return i;
        return -1;
    }
    public int lastIndexOf(int c) {
        for (int i = value.length - 1; i >= 0; i--) if (value[i] == c) return i;
        return -1;
    }
    public int indexOf(String s) { return indexOf(s, 0); }
    public int indexOf(String s, int from) {
        outer:
        for (int i = Math.max(from, 0); i <= value.length - s.value.length; i++) {
            for (int j = 0; j < s.value.length; j++) if (value[i + j] != s.value[j]) continue outer;
            return i;
        }
        return -1;
    }
    public boolean contains(CharSequence s) { return indexOf(s.toString()) >= 0; }
    public boolean startsWith(String p) { return p.length() <= length() && regionMatches(0, p, 0, p.length()); }
    public boolean endsWith(String p) { return p.length() <= length() && regionMatches(length() - p.length(), p, 0, p.length()); }
    public boolean regionMatches(int at, String o, int oat, int len) {
        if (at < 0 || oat < 0 || at + len > length() || oat + len > o.length()) return false;
        for (int i = 0; i < len; i++) if (value[at + i] != o.value[oat + i]) return false;
        return true;
    }
    public String substring(int b) { return substring(b, value.length); }
    public String substring(int b, int e) {
        if (b < 0 || e > value.length || b > e) throw new StringIndexOutOfBoundsException("begin " + b + ", end " + e + ", length " + value.length);
        if (b == 0 && e == value.length) return this;
        return new String(value, b, e - b);
    }
    public CharSequence subSequence(int b, int e) { return substring(b, e); }
    public String concat(String s) {
        if (s.isEmpty()) return this;
        char[] r = new char[value.length + s.value.length];
        System.arraycopy(value, 0, r, 0, value.length);
        System.arraycopy(s.value, 0, r, value.length, s.value.length);
        return new String(0, r);
    }
    public String replace(char a, char b) {
        char[] r = toCharArray();
        for (int i = 0; i < r.length; i++) if (r[i] == a) r[i] = b;
        return new String(0, r);
    }
    public String trim() {
        int b = 0, e = value.length;
        while (b < e && value[b] <= ' ') b++;
        while (e > b && value[e - 1] <= ' ') e--;
        return substring(b, e);
    }
    public String toUpperCase() {
        char[] r = toCharArray();
        for (int i = 0; i < r.length; i++) r[i] = Character.toUpperCase(r[i]);
        return new String(0, r);
    }
    public String toLowerCase() {
        char[] r = toCharArray();
        for (int i = 0; i < r.length; i++) r[i] = Character.toLowerCase(r[i]);
        return new String(0, r);
    }
    /* Only literal separators for now: a regular expression engine comes with libcore. */
    public String[] split(String regex) {
        for (int i = 0; i < regex.length(); i++)
            if (".$|()[{^?*+\\".indexOf(regex.charAt(i)) >= 0 && !(regex.length() == 2 && regex.charAt(0) == '\\'))
                throw new UnsupportedOperationException("split with a regular expression: " + regex);
        String sep = regex.length() == 2 && regex.charAt(0) == '\\' ? regex.substring(1) : regex;
        java.util.ArrayList<String> parts = new java.util.ArrayList<>();
        int start = 0;
        for (int at; (at = indexOf(sep, start)) >= 0 && !sep.isEmpty(); start = at + sep.length()) parts.add(substring(start, at));
        parts.add(substring(start));
        int n = parts.size();
        while (n > 0 && parts.get(n - 1).isEmpty()) n--;           /* Java drops trailing empty strings */
        String[] r = new String[n];
        for (int i = 0; i < n; i++) r[i] = parts.get(i);
        return r;
    }
    public String toString() { return this; }
    public String intern() { return this; }

    public static String valueOf(Object o) { return o == null ? "null" : o.toString(); }
    public static String valueOf(char[] c) { return new String(c); }
    public static String valueOf(boolean b) { return b ? "true" : "false"; }
    public static String valueOf(char c) { return new String(0, new char[] { c }); }
    public static String valueOf(int i) { return Integer.toString(i); }
    public static String valueOf(long l) { return Long.toString(l); }
    public static String valueOf(float f) { return Float.toString(f); }
    public static String valueOf(double d) { return Double.toString(d); }
    public static String join(CharSequence sep, CharSequence... parts) {
        StringBuilder sb = new StringBuilder();
        for (int i = 0; i < parts.length; i++) { if (i > 0) sb.append(sep); sb.append(parts[i]); }
        return sb.toString();
    }
}
