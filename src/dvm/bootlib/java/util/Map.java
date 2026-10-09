package java.util;

public interface Map<K, V> {
    int size();
    default boolean isEmpty() { return size() == 0; }
    V get(Object key);
    V put(K key, V value);
    V remove(Object key);
    boolean containsKey(Object key);
    default V getOrDefault(Object key, V def) { V v = get(key); return v != null || containsKey(key) ? v : def; }
}
