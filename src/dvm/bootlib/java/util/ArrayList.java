package java.util;

public class ArrayList<E> implements List<E>, java.io.Serializable, Cloneable {
    private Object[] elements;
    private int size;
    private int modCount;

    public ArrayList() { elements = new Object[10]; }
    public ArrayList(int cap) {
        if (cap < 0) throw new IllegalArgumentException("Illegal Capacity: " + cap);
        elements = new Object[cap];
    }
    public ArrayList(Collection<? extends E> c) { elements = c.toArray(); size = elements.length; }

    private void check(int i) { if (i < 0 || i >= size) throw new IndexOutOfBoundsException("Index " + i + " out of bounds for length " + size); }
    private void grow() { if (size == elements.length) elements = Arrays.copyOf(elements, elements.length * 2 + 1); }

    public int size() { return size; }
    @SuppressWarnings("unchecked")
    public E get(int i) { check(i); return (E) elements[i]; }
    @SuppressWarnings("unchecked")
    public E set(int i, E e) { check(i); E old = (E) elements[i]; elements[i] = e; return old; }
    public boolean add(E e) { grow(); elements[size++] = e; modCount++; return true; }
    public void add(int i, E e) {
        if (i < 0 || i > size) throw new IndexOutOfBoundsException("Index: " + i + ", Size: " + size);
        grow();
        System.arraycopy(elements, i, elements, i + 1, size - i);
        elements[i] = e;
        size++;
        modCount++;
    }
    @SuppressWarnings("unchecked")
    public E remove(int i) {
        check(i);
        E old = (E) elements[i];
        System.arraycopy(elements, i + 1, elements, i, size - i - 1);
        elements[--size] = null;
        modCount++;
        return old;
    }
    public boolean remove(Object o) { int i = indexOf(o); if (i < 0) return false; remove(i); return true; }
    public void clear() { for (int i = 0; i < size; i++) elements[i] = null; size = 0; modCount++; }
    public int indexOf(Object o) {
        for (int i = 0; i < size; i++) if (o == null ? elements[i] == null : o.equals(elements[i])) return i;
        return -1;
    }
    public boolean contains(Object o) { return indexOf(o) >= 0; }
    public Object[] toArray() { return Arrays.copyOf(elements, size); }
    @SuppressWarnings("unchecked")
    public <T> T[] toArray(T[] a) {
        if (a.length < size) a = (T[]) Arrays.copyOf(elements, size, a.getClass());
        else System.arraycopy(elements, 0, a, 0, size);
        if (a.length > size) a[size] = null;
        return a;
    }
    public Iterator<E> iterator() {
        return new Iterator<E>() {
            int at, expected = modCount;
            public boolean hasNext() { return at < size; }
            @SuppressWarnings("unchecked")
            public E next() {
                if (modCount != expected) throw new ConcurrentModificationException();
                if (at >= size) throw new NoSuchElementException();
                return (E) elements[at++];
            }
        };
    }
    public String toString() {
        StringBuilder sb = new StringBuilder("[");
        for (int i = 0; i < size; i++) { if (i > 0) sb.append(", "); sb.append(elements[i] == this ? "(this Collection)" : elements[i]); }
        return sb.append(']').toString();
    }
    public int hashCode() { int h = 1; for (int i = 0; i < size; i++) h = 31 * h + (elements[i] == null ? 0 : elements[i].hashCode()); return h; }
    public boolean equals(Object o) {
        if (!(o instanceof List)) return false;
        List<?> l = (List<?>) o;
        if (l.size() != size) return false;
        for (int i = 0; i < size; i++) if (!Objects.equals(elements[i], l.get(i))) return false;
        return true;
    }
}
