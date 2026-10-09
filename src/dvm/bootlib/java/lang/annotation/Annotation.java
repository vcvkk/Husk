package java.lang.annotation;

public interface Annotation {
    boolean equals(Object o);
    int hashCode();
    String toString();
    Class<? extends Annotation> annotationType();
}
