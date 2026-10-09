package java.util;

public interface Comparator<T> {
    int compare(T a, T b);
    default Comparator<T> reversed() { final Comparator<T> self = this; return new Comparator<T>() { public int compare(T a, T b) { return self.compare(b, a); } }; }
}
