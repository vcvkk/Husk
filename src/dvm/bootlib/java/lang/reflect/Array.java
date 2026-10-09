package java.lang.reflect;

public final class Array {
    private Array() {}
    public static Object newInstance(Class<?> component, int length) { return newArray(component, length); }
    public static Object newInstance(Class<?> component, int... dimensions) {
        if (dimensions.length == 0) throw new IllegalArgumentException("Empty dimensions array");
        return newMulti(component, dimensions, 0);
    }
    private static Object newMulti(Class<?> component, int[] dims, int at) {
        if (at == dims.length - 1) return newArray(component, dims[at]);
        Class<?> inner = component;
        for (int i = at + 1; i < dims.length; i++) inner = arrayClassOf(inner);
        Object[] outer = (Object[]) newArray(inner, dims[at]);
        for (int i = 0; i < outer.length; i++) outer[i] = newMulti(component, dims, at + 1);
        return outer;
    }
    public static int getLength(Object array) { return getLength0(array); }
    private static native Object newArray(Class<?> component, int length);
    private static native Class<?> arrayClassOf(Class<?> component);
    private static native int getLength0(Object array);
}
