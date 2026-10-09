package java.lang;

public final class Character implements java.io.Serializable, Comparable<Character> {
    public static final char MIN_VALUE = '\u0000', MAX_VALUE = '￿';
    @SuppressWarnings("unchecked")
    public static final Class<Character> TYPE = (Class<Character>) Class.getPrimitiveClass("char");
    private final char value;
    public Character(char v) { value = v; }
    public static Character valueOf(char c) { return new Character(c); }
    public char charValue() { return value; }
    public boolean equals(Object o) { return o instanceof Character && ((Character) o).value == value; }
    public int hashCode() { return value; }
    public int compareTo(Character o) { return value - o.value; }
    public String toString() { return String.valueOf(value); }
    public static String toString(char c) { return String.valueOf(c); }
    public static boolean isDigit(char c) { return c >= '0' && c <= '9'; }
    public static boolean isLetter(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c >= 0xc0; }
    public static boolean isLetterOrDigit(char c) { return isLetter(c) || isDigit(c); }
    public static boolean isWhitespace(char c) { return c == ' ' || (c >= 9 && c <= 13) || (c >= 0x1c && c <= 0x1f); }
    public static boolean isUpperCase(char c) { return c >= 'A' && c <= 'Z'; }
    public static boolean isLowerCase(char c) { return c >= 'a' && c <= 'z'; }
    public static char toUpperCase(char c) { return c >= 'a' && c <= 'z' ? (char) (c - 32) : c; }
    public static char toLowerCase(char c) { return c >= 'A' && c <= 'Z' ? (char) (c + 32) : c; }
    public static int digit(char c, int radix) {
        int d = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'z' ? c - 'a' + 10 : c >= 'A' && c <= 'Z' ? c - 'A' + 10 : -1;
        return d < radix ? d : -1;
    }
    public static char forDigit(int d, int radix) { return d < 0 || d >= radix ? 0 : (char) (d < 10 ? '0' + d : 'a' + d - 10); }
    public static int compare(char a, char b) { return a - b; }
}
