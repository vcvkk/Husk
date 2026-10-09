package java.lang;

public final class Math {
    public static final double PI = 3.141592653589793, E = 2.718281828459045;
    private static long seed = 0x5DEECE66DL;
    private Math() {}
    public static int abs(int a) { return a < 0 ? -a : a; }
    public static long abs(long a) { return a < 0 ? -a : a; }
    public static float abs(float a) { return Float.intBitsToFloat(Float.floatToRawIntBits(a) & 0x7fffffff); }
    public static double abs(double a) { return Double.longBitsToDouble(Double.doubleToRawLongBits(a) & 0x7fffffffffffffffL); }
    public static int max(int a, int b) { return a >= b ? a : b; }
    public static long max(long a, long b) { return a >= b ? a : b; }
    public static int min(int a, int b) { return a <= b ? a : b; }
    public static long min(long a, long b) { return a <= b ? a : b; }
    public static float max(float a, float b) { return a != a ? a : (a == 0 && b == 0) ? (Float.floatToRawIntBits(a) == 0 ? a : b) : a >= b ? a : b; }
    public static float min(float a, float b) { return a != a ? a : (a == 0 && b == 0) ? (Float.floatToRawIntBits(a) != 0 ? a : b) : a <= b ? a : b; }
    public static double max(double a, double b) { return a != a ? a : (a == 0 && b == 0) ? (Double.doubleToRawLongBits(a) == 0 ? a : b) : a >= b ? a : b; }
    public static double min(double a, double b) { return a != a ? a : (a == 0 && b == 0) ? (Double.doubleToRawLongBits(a) != 0 ? a : b) : a <= b ? a : b; }
    public static native double sqrt(double a);
    public static native double cbrt(double a);
    public static native double sin(double a);
    public static native double cos(double a);
    public static native double tan(double a);
    public static native double asin(double a);
    public static native double acos(double a);
    public static native double atan(double a);
    public static native double atan2(double y, double x);
    public static native double exp(double a);
    public static native double log(double a);
    public static native double log10(double a);
    public static native double pow(double a, double b);
    public static native double floor(double a);
    public static native double ceil(double a);
    public static native double rint(double a);
    public static long round(double a) { return (long) floor(a + 0.5); }
    public static int round(float a) { return (int) floor(a + 0.5f); }
    public static double toRadians(double d) { return d / 180.0 * PI; }
    public static double toDegrees(double r) { return r * 180.0 / PI; }
    public static double hypot(double x, double y) { return sqrt(x * x + y * y); }
    public static int floorDiv(int a, int b) { int q = a / b; if ((a % b != 0) && ((a ^ b) < 0)) q--; return q; }
    public static int floorMod(int a, int b) { return a - floorDiv(a, b) * b; }
    public static int addExact(int a, int b) { int r = a + b; if (((a ^ r) & (b ^ r)) < 0) throw new ArithmeticException("integer overflow"); return r; }
    public static double signum(double d) { return d > 0 ? 1.0 : d < 0 ? -1.0 : d; }
    public static double random() {
        seed = (seed * 0x5DEECE66DL + 0xBL) & ((1L << 48) - 1);
        return (seed >>> 22) / (double) (1L << 26);
    }
}
