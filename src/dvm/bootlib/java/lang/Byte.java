package java.lang;

public final class Byte extends Number implements Comparable<Byte> {
    public static final byte MIN_VALUE = (byte) -(1 << (8 - 1)), MAX_VALUE = (byte) ((1 << (8 - 1)) - 1);
    @SuppressWarnings("unchecked")
    public static final Class<Byte> TYPE = (Class<Byte>) Class.getPrimitiveClass("byte");
    private final byte value;
    public Byte(byte v) { value = v; }
    public static Byte valueOf(byte v) { return new Byte(v); }
    public static byte parseByte(String s) {
        int v = Integer.parseInt(s);
        if (v < MIN_VALUE || v > MAX_VALUE) throw new NumberFormatException("Value out of range. Value:\"" + s + "\"");
        return (byte) v;
    }
    public int intValue() { return value; }
    public long longValue() { return value; }
    public float floatValue() { return value; }
    public double doubleValue() { return value; }
    public byte byteValue() { return value; }
    public boolean equals(Object o) { return o instanceof Byte && ((Byte) o).value == value; }
    public int hashCode() { return value; }
    public int compareTo(Byte o) { return value - o.value; }
    public String toString() { return Integer.toString(value); }
    public static String toString(byte v) { return Integer.toString(v); }
}
