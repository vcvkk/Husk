public class Arith {
    static long fact(int n) { return n <= 1 ? 1 : n * fact(n - 1); }
    static double mix(int a, long b, float c, double d, char e, short f, byte g, boolean h, long i, double j) {
        return a + b + c + d + e + f + g + (h ? 1 : 0) + i + j;
    }
    public static void main(String[] args) {
        int max = Integer.MAX_VALUE, min = Integer.MIN_VALUE;
        System.out.println((max + 1) + " " + (min - 1) + " " + (min / -1) + " " + (min % -1) + " " + (-7 / 2) + " " + (-7 % 2) + " " + (7 % -2));
        long lmax = Long.MAX_VALUE;
        System.out.println((lmax + 1) + " " + (Long.MIN_VALUE / -1) + " " + (-7L / 2) + " " + (-7L % 2) + " " + fact(20) + " " + fact(21));
        System.out.println((1 << 33) + " " + (1L << 65) + " " + (-16 >> 2) + " " + (-16 >>> 28) + " " + (-16L >>> 60) + " " + (5 & 3) + " " + (5 | 3) + " " + (5 ^ 3) + " " + (~5));
        System.out.println((int) 3.99 + " " + (int) -3.99 + " " + (int) Double.NaN + " " + (int) 1e20 + " " + (int) -1e20 + " " + (long) 1e30 + " " + (long) Float.NEGATIVE_INFINITY);
        System.out.println((byte) 200 + " " + (short) 70000 + " " + (int) (char) -1 + " " + (char) 65 + " " + (byte) -129);
        double[] ds = { 0.1, 1.0, 100.0, 1e7, 1.0e-3, 9.99e-4, 123456.789, 1.0 / 3, 2.0 / 3, 1e21, 1e-10, -0.0, 3.0e10, 12345678.9, 1.7976931348623157E308, 4.9e-324, 100.5, 0.5 };
        for (double d : ds) System.out.print(d + " ");
        System.out.println();
        float[] fs = { 0.1f, 1.0f / 3, 3.4028235e38f, 1.4e-45f, 1234567.0f, 1.0e7f, 16777216.0f, 0.001f, 33.333f };
        for (float f : fs) System.out.print(f + " ");
        System.out.println();
        System.out.println((0.1 + 0.2) + " " + (0.1f + 0.2f) + " " + (1.0 / 0) + " " + (-1.0 / 0) + " " + (0.0 / 0) + " " + (5.5 % 2) + " " + (-5.5f % 2));
        double nan = Double.NaN;
        System.out.println((nan < 1) + " " + (nan > 1) + " " + (nan == nan) + " " + (nan != nan) + " " + Double.compare(nan, 1) + " " + Double.compare(0.0, -0.0));
        System.out.println(mix(1, 2L, 3.5f, 4.25, 'A', (short) 6, (byte) 7, true, 1L << 40, 0.125));
        int x = 0;
        for (int i = 0; i < 1000; i++) { x += i * i; x ^= x << 3; x += x >>> 7; }
        long y = 17;
        for (int i = 0; i < 1000; i++) y = y * 6364136223846793005L + 1442695040888963407L;
        System.out.println("hashes " + x + " " + y);
        System.out.println(Math.abs(-5) + " " + Math.max(3L, 9L) + " " + Math.min(-0.0, 0.0) + " " + Math.sqrt(2) + " " + Math.floor(-1.5) + " " + Math.ceil(-1.5) + " " + Math.round(2.5) + " " + Math.round(-2.5) + " " + Math.pow(2, 10));
        System.out.println(Integer.toHexString(-1) + " " + Integer.toBinaryString(10) + " " + Long.toHexString(255L) + " " + Integer.parseInt("-123") + " " + Long.parseLong("9223372036854775807") + " " + Integer.toString(255, 16));
        System.out.println(Integer.bitCount(255) + " " + Integer.numberOfLeadingZeros(1) + " " + Long.numberOfTrailingZeros(64) + " " + Character.digit('7', 10));
    }
}
