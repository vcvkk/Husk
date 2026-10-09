package java.lang;

public interface Iterable<T> {
    java.util.Iterator<T> iterator();
    default void forEach(java.util.function.Consumer<? super T> action) { for (T t : this) action.accept(t); }
}
