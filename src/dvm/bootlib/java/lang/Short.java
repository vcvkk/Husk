package java.lang;

public final class Short extends Number implements Comparable<Short> {
    public static final short MIN_VALUE = (short) -(1 << (16 - 1)), MAX_VALUE = (short) ((1 << (16 - 1)) - 1);
    @SuppressWarnings("unchecked")
    public static final Class<Short> TYPE = (Class<Short>) Class.getPrimitiveClass("short");
    private final short value;
    public Short(short v) { value = v; }
    public static Short valueOf(short v) { return new Short(v); }
    public static short parseShort(String s) {
        int v = Integer.parseInt(s);
        if (v < MIN_VALUE || v > MAX_VALUE) throw new NumberFormatException("Value out of range. Value:\"" + s + "\"");
        return (short) v;
    }
    public int intValue() { return value; }
    public long longValue() { return value; }
    public float floatValue() { return value; }
    public double doubleValue() { return value; }
    public short shortValue() { return value; }
    public boolean equals(Object o) { return o instanceof Short && ((Short) o).value == value; }
    public int hashCode() { return value; }
    public int compareTo(Short o) { return value - o.value; }
    public String toString() { return Integer.toString(value); }
    public static String toString(short v) { return Integer.toString(v); }
}
