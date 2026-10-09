package java.util;

public final class Arrays {
    private Arrays() {}
    public static char[] copyOf(char[] a, int n) { char[] r = new char[n]; System.arraycopy(a, 0, r, 0, Math.min(n, a.length)); return r; }
    public static int[] copyOf(int[] a, int n) { int[] r = new int[n]; System.arraycopy(a, 0, r, 0, Math.min(n, a.length)); return r; }
    public static long[] copyOf(long[] a, int n) { long[] r = new long[n]; System.arraycopy(a, 0, r, 0, Math.min(n, a.length)); return r; }
    public static byte[] copyOf(byte[] a, int n) { byte[] r = new byte[n]; System.arraycopy(a, 0, r, 0, Math.min(n, a.length)); return r; }
    @SuppressWarnings("unchecked")
    public static <T> T[] copyOf(T[] a, int n) { return (T[]) copyOf(a, n, a.getClass()); }
    @SuppressWarnings("unchecked")
    public static <T, U> T[] copyOf(U[] a, int n, Class<? extends T[]> type) {
        T[] r = (T[]) newArray(type.getComponentType(), n);
        System.arraycopy(a, 0, r, 0, Math.min(n, a.length));
        return r;
    }
    private static Object[] newArray(Class<?> component, int n) {
        if (component == Object.class) return new Object[n];
        return newArray0(component, n);
    }
    private static native Object[] newArray0(Class<?> component, int n);
    public static int[] copyOfRange(int[] a, int from, int to) { int[] r = new int[to - from]; System.arraycopy(a, from, r, 0, Math.min(a.length - from, to - from)); return r; }

    public static void fill(int[] a, int v) { for (int i = 0; i < a.length; i++) a[i] = v; }
    public static void fill(long[] a, long v) { for (int i = 0; i < a.length; i++) a[i] = v; }
    public static void fill(char[] a, char v) { for (int i = 0; i < a.length; i++) a[i] = v; }
    public static void fill(boolean[] a, boolean v) { for (int i = 0; i < a.length; i++) a[i] = v; }
    public static void fill(Object[] a, Object v) { for (int i = 0; i < a.length; i++) a[i] = v; }

    public static boolean equals(int[] a, int[] b) {
        if (a == b) return true;
        if (a == null || b == null || a.length != b.length) return false;
        for (int i = 0; i < a.length; i++) if (a[i] != b[i]) return false;
        return true;
    }
    public static int hashCode(Object[] a) {
        if (a == null) return 0;
        int h = 1;
        for (Object o : a) h = 31 * h + (o == null ? 0 : o.hashCode());
        return h;
    }
    public static String toString(int[] a) {
        if (a == null) return "null";
        StringBuilder sb = new StringBuilder("[");
        for (int i = 0; i < a.length; i++) { if (i > 0) sb.append(", "); sb.append(a[i]); }
        return sb.append(']').toString();
    }
    public static String toString(long[] a) {
        if (a == null) return "null";
        StringBuilder sb = new StringBuilder("[");
        for (int i = 0; i < a.length; i++) { if (i > 0) sb.append(", "); sb.append(a[i]); }
        return sb.append(']').toString();
    }
    public static String toString(Object[] a) {
        if (a == null) return "null";
        StringBuilder sb = new StringBuilder("[");
        for (int i = 0; i < a.length; i++) { if (i > 0) sb.append(", "); sb.append(a[i]); }
        return sb.append(']').toString();
    }
    public static void sort(int[] a) {
        for (int i = 1; i < a.length; i++) { int v = a[i], j = i - 1; while (j >= 0 && a[j] > v) { a[j + 1] = a[j]; j--; } a[j + 1] = v; }
    }
    @SuppressWarnings({ "unchecked", "rawtypes" })
    public static void sort(Object[] a) { sort(a, new Comparator<Object>() { public int compare(Object x, Object y) { return ((Comparable) x).compareTo(y); } }); }
    /* stable: insertion sort, which is enough for now */
    public static <T> void sort(T[] a, Comparator<? super T> c) {
        for (int i = 1; i < a.length; i++) { T v = a[i]; int j = i - 1; while (j >= 0 && c.compare(a[j], v) > 0) { a[j + 1] = a[j]; j--; } a[j + 1] = v; }
    }
    @SafeVarargs
    public static <T> List<T> asList(T... a) { ArrayList<T> l = new ArrayList<>(a.length); for (T t : a) l.add(t); return l; }
}
