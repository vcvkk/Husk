package java.util;

public class HashMap<K, V> implements Map<K, V>, java.io.Serializable {
    static final class Node<K, V> {
        final int hash; final K key; V value; Node<K, V> next;
        Node(int h, K k, V v, Node<K, V> n) { hash = h; key = k; value = v; next = n; }
    }
    private Node<K, V>[] table;
    private int size;

    @SuppressWarnings("unchecked")
    public HashMap() { table = (Node<K, V>[]) new Node[16]; }

    private static int hash(Object k) { int h = k == null ? 0 : k.hashCode(); return h ^ (h >>> 16); }
    private Node<K, V> find(Object k) {
        int h = hash(k);
        for (Node<K, V> n = table[h & (table.length - 1)]; n != null; n = n.next)
            if (n.hash == h && (n.key == k || (k != null && k.equals(n.key)))) return n;
        return null;
    }
    public int size() { return size; }
    public V get(Object k) { Node<K, V> n = find(k); return n == null ? null : n.value; }
    public boolean containsKey(Object k) { return find(k) != null; }
    @SuppressWarnings("unchecked")
    public V put(K k, V v) {
        Node<K, V> n = find(k);
        if (n != null) { V old = n.value; n.value = v; return old; }
        if (size + 1 > table.length * 3 / 4) {
            Node<K, V>[] old = table;
            table = (Node<K, V>[]) new Node[old.length * 2];
            for (Node<K, V> e : old)
                for (Node<K, V> x = e; x != null; ) {
                    Node<K, V> next = x.next;
                    int i = x.hash & (table.length - 1);
                    x.next = table[i];
                    table[i] = x;
                    x = next;
                }
        }
        int h = hash(k), i = h & (table.length - 1);
        table[i] = new Node<>(h, k, v, table[i]);
        size++;
        return null;
    }
    public V remove(Object k) {
        int h = hash(k), i = h & (table.length - 1);
        for (Node<K, V> n = table[i], prev = null; n != null; prev = n, n = n.next)
            if (n.hash == h && (n.key == k || (k != null && k.equals(n.key)))) {
                if (prev == null) table[i] = n.next; else prev.next = n.next;
                size--;
                return n.value;
            }
        return null;
    }
    public void clear() { Arrays.fill(table, null); size = 0; }
}
