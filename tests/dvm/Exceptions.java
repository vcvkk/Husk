public class Exceptions {
    static class MyException extends Exception {
        final int code;
        MyException(String m, int code) { super(m); this.code = code; }
    }
    static class Res implements AutoCloseable {
        final String name; final boolean failClose;
        Res(String name, boolean failClose) { this.name = name; this.failClose = failClose; System.out.println("open " + name); }
        public void close() { System.out.println("close " + name); if (failClose) throw new IllegalStateException("close " + name); }
    }
    static class Bad {
        static int value = compute();
        static int compute() { throw new RuntimeException("bad static init"); }
    }

    static void thrower(int depth) throws MyException {
        if (depth == 0) throw new MyException("deep", 7);
        thrower(depth - 1);
    }
    @SuppressWarnings("finally")
    static int finallyReturns() { try { throw new RuntimeException("lost"); } finally { return 5; } }
    static String order() {
        StringBuilder sb = new StringBuilder();
        try {
            try { sb.append("a"); throw new IllegalArgumentException("x"); }
            finally { sb.append("b"); }
        } catch (IllegalArgumentException e) { sb.append("c"); }
        finally { sb.append("d"); }
        return sb.toString();
    }
    static int recurse(int n) { return recurse(n + 1) + 1; }
    static String kind(Runnable r) {
        try { r.run(); return "none"; }
        catch (RuntimeException e) { return e.getClass().getSimpleName(); }
    }
    static Object nothing() { return null; }
    static int[] arr;

    public static void main(String[] args) throws Exception {
        try { thrower(10); } catch (MyException e) { System.out.println("caught " + e.getMessage() + " code " + e.code); }
        System.out.println("finally returns " + finallyReturns());
        System.out.println("order " + order());
        System.out.println(kind(new Runnable() { public void run() { nothing().toString(); } }));
        System.out.println(kind(new Runnable() { public void run() { int z = 0; System.out.println(10 / z); } }));
        System.out.println(kind(new Runnable() { public void run() { long z = 0; System.out.println(10L % z); } }));
        System.out.println(kind(new Runnable() { public void run() { int[] a = new int[3]; a[3] = 1; } }));
        System.out.println(kind(new Runnable() { public void run() { int[] a = new int[-1]; } }));
        System.out.println(kind(new Runnable() { public void run() { Object o = "s"; Integer i = (Integer) o; } }));
        System.out.println(kind(new Runnable() { public void run() { Object[] o = new String[1]; o[0] = 1; } }));
        System.out.println(kind(new Runnable() { public void run() { System.out.println(arr.length); } }));
        System.out.println(kind(new Runnable() { public void run() { throw new UnsupportedOperationException("u"); } }));
        try { recurse(0); } catch (StackOverflowError e) { System.out.println("caught StackOverflowError"); }
        try { System.out.println(Bad.value); } catch (ExceptionInInitializerError e) { System.out.println("EIIE cause: " + e.getCause().getMessage()); }
        try { System.out.println(Bad.value); } catch (NoClassDefFoundError e) { System.out.println("then NoClassDefFoundError"); }
        try (Res a = new Res("a", false); Res b = new Res("b", true)) {
            System.out.println("body");
            throw new RuntimeException("body failed");
        } catch (RuntimeException e) {
            System.out.println("caught " + e.getMessage() + ", suppressed " + e.getSuppressed().length + ": " + e.getSuppressed()[0].getMessage());
        }
        try {
            try { throw new IllegalStateException("inner"); }
            catch (IllegalStateException e) { throw new RuntimeException("outer", e); }
        } catch (RuntimeException e) { System.out.println(e.getMessage() + " <- " + e.getCause().getMessage()); }
        try { Object o = Integer.valueOf(1); String s = (String) o; }
        catch (ClassCastException | NullPointerException e) { System.out.println("multi-catch " + e.getClass().getName()); }
        Throwable t = new Error("plain");
        System.out.println(t + " / " + new RuntimeException() + " / " + new Exception((String) null).getMessage());
        int caught = 0;
        for (int i = 0; i < 10000; i++) { try { if (i % 3 == 0) throw new RuntimeException(); } catch (RuntimeException e) { caught++; } }
        System.out.println("caught in loop " + caught);
        System.out.println("done; now uncaught:");
        throw new MyException("escaped", 1);
    }
}
