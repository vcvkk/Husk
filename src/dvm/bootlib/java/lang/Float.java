package java.lang;

public final class Float extends Number implements Comparable<Float> {
    public static final float POSITIVE_INFINITY = 1.0f / 0.0f, NEGATIVE_INFINITY = -1.0f / 0.0f, NaN = 0.0f / 0.0f;
    public static final float MAX_VALUE = 0x1.fffffeP+127f, MIN_VALUE = 0x0.000002P-126f;
    @SuppressWarnings("unchecked")
    public static final Class<Float> TYPE = (Class<Float>) Class.getPrimitiveClass("float");
    private final float value;
    public Float(float v) { value = v; }
    public static Float valueOf(float v) { return new Float(v); }
    public static float parseFloat(String s) { return (float) Double.parseDouble(s); }
    public int intValue() { return (int) value; }
    public long longValue() { return (long) value; }
    public float floatValue() { return value; }
    public double doubleValue() { return value; }
    public boolean isNaN() { return value != value; }
    public static boolean isNaN(float f) { return f != f; }
    public static boolean isInfinite(float f) { return f == POSITIVE_INFINITY || f == NEGATIVE_INFINITY; }
    public boolean equals(Object o) { return o instanceof Float && floatToIntBits(((Float) o).value) == floatToIntBits(value); }
    public int hashCode() { return floatToIntBits(value); }
    public int compareTo(Float o) { return compare(value, o.value); }
    public static int compare(float a, float b) {
        if (a < b) return -1;
        if (a > b) return 1;
        int x = floatToIntBits(a), y = floatToIntBits(b);
        return x == y ? 0 : x < y ? -1 : 1;
    }
    public String toString() { return toString(value); }
    public static native String toString(float f);
    public static int floatToIntBits(float f) { return f != f ? 0x7fc00000 : floatToRawIntBits(f); }
    public static native int floatToRawIntBits(float f);
    public static native float intBitsToFloat(int i);
}
