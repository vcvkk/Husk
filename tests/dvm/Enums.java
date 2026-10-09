public class Enums {
    enum Color { RED, GREEN, BLUE }
    enum Op {
        ADD("+") { int apply(int a, int b) { return a + b; } },
        MUL("*") { int apply(int a, int b) { return a * b; } };
        final String sym;
        Op(String s) { sym = s; }
        abstract int apply(int a, int b);
    }
    static String name(Color c) {
        switch (c) { case RED: return "r"; case GREEN: return "g"; default: return "other"; }
    }
    public static void main(String[] args) {
        for (Color c : Color.values()) System.out.print(c + ":" + c.ordinal() + ":" + name(c) + " ");
        System.out.println();
        System.out.println(Color.valueOf("BLUE") + " " + (Color.valueOf("GREEN") == Color.GREEN) + " " + Color.RED.compareTo(Color.BLUE) + " " + Color.BLUE.name());
        try { Color.valueOf("PINK"); } catch (IllegalArgumentException e) { System.out.println("IAE " + e.getMessage()); }
        for (Op op : Op.values()) System.out.print(op + " " + op.sym + " " + op.apply(6, 7) + " " + op.getDeclaringClass().getSimpleName() + "; ");
        System.out.println();
    }
}
