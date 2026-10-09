import java.util.ArrayList;
import java.util.List;
import java.util.function.Function;
import java.util.function.Supplier;

public class Lambdas {
    interface IntOp { int apply(int a, int b); }
    static int twice(IntOp op, int x) { return op.apply(op.apply(x, x), x); }
    static String up(String s) { return s.toUpperCase(); }
    public static void main(String[] args) {
        int base = 10;
        IntOp add = (a, b) -> a + b + base;
        IntOp mul = (a, b) -> a * b;
        System.out.println(twice(add, 3) + " " + twice(mul, 3));
        Function<String, String> f = Lambdas::up;
        Supplier<List<String>> mk = ArrayList::new;
        List<String> l = mk.get();
        l.add(f.apply("abc"));
        l.add(f.apply("xyz"));
        StringBuilder sb = new StringBuilder();
        l.forEach(s -> sb.append(s).append(';'));
        Runnable r = () -> System.out.println("lambda runnable " + sb);
        r.run();
        l.sort((x, y) -> y.compareTo(x));
        System.out.println(l);
    }
}
