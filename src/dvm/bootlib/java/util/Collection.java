package java.util;

public interface Collection<E> extends Iterable<E> {
    int size();
    default boolean isEmpty() { return size() == 0; }
    boolean add(E e);
    boolean contains(Object o);
    Object[] toArray();
    <T> T[] toArray(T[] a);
}
