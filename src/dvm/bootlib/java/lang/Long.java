package java.lang;

public final class Long extends Number implements Comparable<Long> {
    public static final long MIN_VALUE = 0x8000000000000000L, MAX_VALUE = 0x7fffffffffffffffL;
    public static final int SIZE = 64;
    @SuppressWarnings("unchecked")
    public static final Class<Long> TYPE = (Class<Long>) Class.getPrimitiveClass("long");
    private static final char[] DIGITS = "0123456789abcdefghijklmnopqrstuvwxyz".toCharArray();

    private final long value;
    public Long(long v) { value = v; }
    public static Long valueOf(long v) { return new Long(v); }
    public int intValue() { return (int) value; }
    public long longValue() { return value; }
    public float floatValue() { return value; }
    public double doubleValue() { return value; }
    public boolean equals(Object o) { return o instanceof Long && ((Long) o).value == value; }
    public int hashCode() { return hashCode(value); }
    public static int hashCode(long v) { return (int) (v ^ (v >>> 32)); }
    public int compareTo(Long o) { return compare(value, o.value); }
    public static int compare(long a, long b) { return a < b ? -1 : a == b ? 0 : 1; }
    public String toString() { return toString(value); }
    public static String toString(long v) { return toString(v, 10); }
    public static String toString(long v, int radix) {
        if (v == 0) return "0";
        char[] buf = new char[65];
        int at = 65;
        boolean neg = v < 0;
        /* work in negatives so MIN_VALUE needs no special case */
        if (!neg) v = -v;
        while (v != 0) { buf[--at] = DIGITS[(int) -(v % radix)]; v /= radix; }
        if (neg) buf[--at] = '-';
        return new String(buf, at, 65 - at);
    }
    static String toUnsignedString(long v, int shift) {
        if (v == 0) return "0";
        char[] buf = new char[64];
        int at = 64, mask = (1 << shift) - 1;
        while (v != 0) { buf[--at] = DIGITS[(int) (v & mask)]; v >>>= shift; }
        return new String(buf, at, 64 - at);
    }
    public static String toHexString(long v) { return toUnsignedString(v, 4); }
    public static String toBinaryString(long v) { return toUnsignedString(v, 1); }
    public static long parseLong(String s) { return parseLong(s, 10); }
    public static long parseLong(String s, int radix) {
        if (s == null) throw new NumberFormatException("null");
        int i = 0, n = s.length();
        boolean neg = false;
        if (n > 0 && (s.charAt(0) == '-' || s.charAt(0) == '+')) { neg = s.charAt(0) == '-'; i = 1; }
        if (i >= n) throw new NumberFormatException("For input string: \"" + s + "\"");
        long r = 0;
        for (; i < n; i++) {
            int d = Character.digit(s.charAt(i), radix);
            if (d < 0) throw new NumberFormatException("For input string: \"" + s + "\"");
            long nr = r * radix - d;
            if (nr > r) throw new NumberFormatException("For input string: \"" + s + "\"");
            r = nr;
        }
        if (!neg) {
            if (r == MIN_VALUE) throw new NumberFormatException("For input string: \"" + s + "\"");
            r = -r;
        }
        return r;
    }
    public static long max(long a, long b) { return Math.max(a, b); }
    public static long min(long a, long b) { return Math.min(a, b); }
    public static int bitCount(long i) { int n = 0; while (i != 0) { i &= i - 1; n++; } return n; }
    public static int numberOfTrailingZeros(long i) { if (i == 0) return 64; int n = 0; while ((i & 1) == 0) { i >>>= 1; n++; } return n; }
    public static int numberOfLeadingZeros(long i) { if (i == 0) return 64; int n = 0; while (i > 0) { i <<= 1; n++; } return n; }
}
