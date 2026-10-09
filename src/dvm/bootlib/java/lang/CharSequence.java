package java.lang;

public interface CharSequence {
    int length();
    char charAt(int i);
    CharSequence subSequence(int b, int e);
    String toString();
}
