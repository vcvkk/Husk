import java.util.ArrayList;
import java.util.List;

public class Dispatch {
    interface Shape { double area(); default String describe() { return name() + " with area " + area(); } String name(); }
    interface Named { default String tag() { return "named"; } }
    interface Loud extends Named { default String tag() { return "LOUD " + Named.super.tag(); } }
    static abstract class Base implements Shape {
        String who() { return "Base"; }
        public String name() { return who() + "/" + getClass().getSimpleName(); }
        public String toString() { return "<" + name() + ">"; }
        abstract int sides();
    }
    static class Square extends Base implements Loud {
        final double s;
        Square(double s) { this.s = s; }
        public double area() { return s * s; }
        String who() { return "Square(" + super.who() + ")"; }
        int sides() { return 4; }
    }
    static class Circle extends Base {
        final double r;
        Circle(double r) { this.r = r; }
        public double area() { return 3 * r * r; }
        int sides() { return 0; }
        public String describe() { return "round " + super.describe(); }
    }
    static class Unit extends Square {
        Unit() { super(1); }
        String who() { return "Unit:" + super.who(); }
        public String tag() { return "unit " + super.tag(); }
    }
    static class Counter {
        private int n;
        private void bump() { n++; }
        void twice() { bump(); bump(); }
    }
    static int depth(Object o) {
        int d = 0;
        for (Class<?> c = o.getClass(); c != null; c = c.getSuperclass()) d++;
        return d;
    }

    public static void main(String[] args) {
        List<Shape> shapes = new ArrayList<>();
        shapes.add(new Square(3));
        shapes.add(new Circle(2));
        shapes.add(new Unit());
        shapes.add(new Shape() {
            public double area() { return 42; }
            public String name() { return "anon"; }
        });
        for (Shape s : shapes) {
            System.out.println(s.describe());
            if (s instanceof Base) System.out.println("  sides=" + ((Base) s).sides() + " str=" + s + " depth=" + depth(s));
            if (s instanceof Named) System.out.println("  tag=" + ((Named) s).tag());
        }
        Counter c = new Counter();
        c.twice();
        System.out.println("counter " + c.n);
        Object o = new Unit();
        System.out.println("Unit is Shape: " + (o instanceof Shape) + ", Loud: " + (o instanceof Loud) + ", Circle: " + (o instanceof Circle));
        Runnable r = new Runnable() { public void run() { System.out.println("ran anonymous runnable"); } };
        r.run();
        Object[] arr = new String[] { "a", "b" };
        System.out.println("String[] instanceof Object[]: " + (arr instanceof Object[]) + ", " + (arr instanceof String[]));
        System.out.println(arr.getClass().getName() + " " + new int[0].getClass().getName() + " " + int.class.getName());
        Shape[] many = new Shape[] { new Square(1), new Circle(1) };
        double total = 0;
        for (Shape s : many) total += s.area();
        System.out.println("total " + total);
        System.out.println("hash consistent: " + (o.hashCode() == o.hashCode()) + ", equals self: " + o.equals(o));
    }
}
