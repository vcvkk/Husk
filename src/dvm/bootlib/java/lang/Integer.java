package java.lang;

public final class Integer extends Number implements Comparable<Integer> {
    public static final int MIN_VALUE = 0x80000000, MAX_VALUE = 0x7fffffff, SIZE = 32;
    @SuppressWarnings("unchecked")
    public static final Class<Integer> TYPE = (Class<Integer>) Class.getPrimitiveClass("int");
    private static final Integer[] CACHE = new Integer[256];
    static { for (int i = 0; i < 256; i++) CACHE[i] = new Integer(i - 128); }

    private final int value;
    public Integer(int v) { value = v; }
    public static Integer valueOf(int v) { return v >= -128 && v <= 127 ? CACHE[v + 128] : new Integer(v); }
    public static Integer valueOf(String s) { return valueOf(parseInt(s)); }
    public int intValue() { return value; }
    public long longValue() { return value; }
    public float floatValue() { return value; }
    public double doubleValue() { return value; }
    public boolean equals(Object o) { return o instanceof Integer && ((Integer) o).value == value; }
    public int hashCode() { return value; }
    public static int hashCode(int v) { return v; }
    public int compareTo(Integer o) { return compare(value, o.value); }
    public static int compare(int a, int b) { return a < b ? -1 : a == b ? 0 : 1; }
    public String toString() { return toString(value); }
    public static String toString(int v) { return Long.toString(v, 10); }
    public static String toString(int v, int radix) { return Long.toString(v, radix); }
    public static String toHexString(int v) { return Long.toUnsignedString(v & 0xffffffffL, 4); }
    public static String toBinaryString(int v) { return Long.toUnsignedString(v & 0xffffffffL, 1); }
    public static String toOctalString(int v) { return Long.toUnsignedString(v & 0xffffffffL, 3); }
    public static int parseInt(String s) { return parseInt(s, 10); }
    public static int parseInt(String s, int radix) {
        long v = Long.parseLong(s, radix);
        if (v < MIN_VALUE || v > MAX_VALUE) throw new NumberFormatException("For input string: \"" + s + "\"");
        return (int) v;
    }
    public static int max(int a, int b) { return Math.max(a, b); }
    public static int min(int a, int b) { return Math.min(a, b); }
    public static int sum(int a, int b) { return a + b; }
    public static int bitCount(int i) { int n = 0; while (i != 0) { i &= i - 1; n++; } return n; }
    public static int signum(int i) { return (i >> 31) | (-i >>> 31); }
    public static int reverse(int i) { int r = 0; for (int k = 0; k < 32; k++) { r = (r << 1) | (i & 1); i >>>= 1; } return r; }
    public static int numberOfLeadingZeros(int i) { if (i == 0) return 32; int n = 0; while (i > 0) { i <<= 1; n++; } return n; }
    public static int numberOfTrailingZeros(int i) { if (i == 0) return 32; int n = 0; while ((i & 1) == 0) { i >>>= 1; n++; } return n; }
    public static int highestOneBit(int i) { return i == 0 ? 0 : MIN_VALUE >>> numberOfLeadingZeros(i); }
    public static int rotateLeft(int i, int d) { return (i << d) | (i >>> -d); }
    public static int rotateRight(int i, int d) { return (i >>> d) | (i << -d); }
}
