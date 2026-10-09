package java.lang;

public final class Double extends Number implements Comparable<Double> {
    public static final double POSITIVE_INFINITY = 1.0 / 0.0, NEGATIVE_INFINITY = -1.0 / 0.0, NaN = 0.0d / 0.0;
    public static final double MAX_VALUE = 0x1.fffffffffffffP+1023, MIN_VALUE = 0x0.0000000000001P-1022;
    @SuppressWarnings("unchecked")
    public static final Class<Double> TYPE = (Class<Double>) Class.getPrimitiveClass("double");
    private final double value;
    public Double(double v) { value = v; }
    public static Double valueOf(double v) { return new Double(v); }
    public static native double parseDouble(String s);
    public int intValue() { return (int) value; }
    public long longValue() { return (long) value; }
    public float floatValue() { return (float) value; }
    public double doubleValue() { return value; }
    public boolean isNaN() { return value != value; }
    public static boolean isNaN(double d) { return d != d; }
    public static boolean isInfinite(double d) { return d == POSITIVE_INFINITY || d == NEGATIVE_INFINITY; }
    public boolean equals(Object o) { return o instanceof Double && doubleToLongBits(((Double) o).value) == doubleToLongBits(value); }
    public int hashCode() { long b = doubleToLongBits(value); return (int) (b ^ (b >>> 32)); }
    public int compareTo(Double o) { return compare(value, o.value); }
    public static int compare(double a, double b) {
        if (a < b) return -1;
        if (a > b) return 1;
        long x = doubleToLongBits(a), y = doubleToLongBits(b);
        return x == y ? 0 : x < y ? -1 : 1;
    }
    public String toString() { return toString(value); }
    public static native String toString(double d);
    public static long doubleToLongBits(double d) { return d != d ? 0x7ff8000000000000L : doubleToRawLongBits(d); }
    public static native long doubleToRawLongBits(double d);
    public static native double longBitsToDouble(long l);
}
