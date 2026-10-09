package java.util;

public interface List<E> extends Collection<E> {
    E get(int i);
    E set(int i, E e);
    void add(int i, E e);
    E remove(int i);
    int indexOf(Object o);
    default void sort(Comparator<? super E> c) {
        Object[] a = toArray();
        Arrays.sort(a, (Comparator) c);
        for (int i = 0; i < a.length; i++) set(i, (E) a[i]);
    }
}
