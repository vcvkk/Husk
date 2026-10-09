package java.lang;

public final class Class<T> implements java.io.Serializable {
    private transient long vmClass;
    private Class() {}
    public native String getName();
    public String getSimpleName() {
        if (isArray()) return getComponentType().getSimpleName() + "[]";
        String n = getName();
        int i = Math.max(n.lastIndexOf('.'), n.lastIndexOf('$'));
        return n.substring(i + 1);
    }
    public String getCanonicalName() {
        if (isArray()) { String c = getComponentType().getCanonicalName(); return c == null ? null : c + "[]"; }
        return getName().replace('$', '.');
    }
    public native boolean isInterface();
    public native boolean isArray();
    public native boolean isPrimitive();
    public boolean isEnum() { return getSuperclass() == Enum.class && this != Enum.class; }
    public native Class<? super T> getSuperclass();
    public native Class<?> getComponentType();
    public native boolean isInstance(Object o);
    public native boolean isAssignableFrom(Class<?> c);
    public static native Class<?> forName(String name) throws ClassNotFoundException;
    static native Class<?> getPrimitiveClass(String name);
    native Object[] getEnumConstantsShared();
    @SuppressWarnings("unchecked")
    public T cast(Object o) {
        if (o != null && !isInstance(o)) throw new ClassCastException("Cannot cast " + o.getClass().getName() + " to " + getName());
        return (T) o;
    }
    public boolean desiredAssertionStatus() { return false; }
    public String toString() { return (isInterface() ? "interface " : isPrimitive() ? "" : "class ") + getName(); }
}
