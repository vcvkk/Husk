public class Statics {
    static final String LIT = "constant";
    static final long BIG = 1234567890123L;
    static final double PI2 = 6.283185307179586;
    static final float F = -2.5f;
    static final char CH = 'Z';
    static final byte BY = -7;
    static final short SH = -300;
    static final boolean YES = true;
    static int counter;
    static String log = "";

    static class A { static { log += "A "; } static int x = 1; }
    static class B extends A { static { log += "B "; } static int y = 2; static void touch() {} }
    static class C { static { log += "C "; } static final int K = 5; }
    interface I { int V = 9; }
    static class Parent {
        Parent() { log += "Parent() "; hook(); }
        void hook() { log += "Parent.hook "; }
    }
    static class Child extends Parent {
        int field = 7;
        { log += "init-block(" + field + ") "; }
        Child() { super(); log += "Child() "; }
        void hook() { log += "Child.hook(field=" + field + ") "; }
    }

    public static void main(String[] args) {
        System.out.println(LIT + " " + BIG + " " + PI2 + " " + F + " " + CH + " " + BY + " " + SH + " " + YES + " " + I.V);
        int k = C.K;
        System.out.println("after constant C.K=" + k + ": [" + log + "]");
        B.touch();
        System.out.println("after B.touch: [" + log + "]");
        log = "";
        new Child();
        System.out.println(log);
        for (int i = 0; i < 5; i++) counter += i;
        System.out.println("counter " + counter + " " + B.x + B.y);
    }
}
