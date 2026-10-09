package java.lang;

public final class Boolean implements java.io.Serializable, Comparable<Boolean> {
    public static final Boolean TRUE = new Boolean(true), FALSE = new Boolean(false);
    @SuppressWarnings("unchecked")
    public static final Class<Boolean> TYPE = (Class<Boolean>) Class.getPrimitiveClass("boolean");
    private final boolean value;
    public Boolean(boolean v) { value = v; }
    public static Boolean valueOf(boolean b) { return b ? TRUE : FALSE; }
    public static Boolean valueOf(String s) { return valueOf(parseBoolean(s)); }
    public static boolean parseBoolean(String s) { return s != null && s.equalsIgnoreCase("true"); }
    public boolean booleanValue() { return value; }
    public boolean equals(Object o) { return o instanceof Boolean && ((Boolean) o).value == value; }
    public int hashCode() { return value ? 1231 : 1237; }
    public int compareTo(Boolean o) { return value == o.value ? 0 : value ? 1 : -1; }
    public String toString() { return value ? "true" : "false"; }
    public static String toString(boolean b) { return b ? "true" : "false"; }
}
