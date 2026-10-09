public class Strings {
    static String classify(String s) {
        switch (s) {
            case "apple": return "fruit";
            case "carrot": return "vegetable";
            case "Aa": return "collides-1";
            case "BB": return "collides-2";
            default: return "unknown";
        }
    }
    static int sparse(int k) {
        switch (k) { case -1000: return 1; case 7: return 2; case 100000: return 3; case 42: return 4; default: return 0; }
    }
    static int dense(int k) {
        switch (k) { case 3: return 30; case 4: return 40; case 5: return 50; case 6: return 60; case 7: return 70; default: return -1; }
    }
    public static void main(String[] args) {
        for (String s : new String[] { "apple", "carrot", "Aa", "BB", "kiwi" }) System.out.print(classify(s) + " ");
        System.out.println();
        for (int k : new int[] { -1000, 7, 100000, 42, 5 }) System.out.print(sparse(k) + "," + dense(k) + " ");
        System.out.println();
        StringBuilder sb = new StringBuilder();
        for (char c = 'a'; c <= 'z'; c++) sb.append(c);
        sb.insert(0, "[").append(']').reverse();
        System.out.println(sb + " len=" + sb.length());
        String s = "  Hello, World  ";
        System.out.println("'" + s.trim() + "' " + s.indexOf("World") + " " + s.toUpperCase().trim() + " " + s.trim().toLowerCase() + " " + s.contains("lo,") + " " + s.trim().startsWith("Hell") + " " + s.trim().endsWith("ld"));
        System.out.println("abc".compareTo("abd") + " " + "b".compareTo("abc") + " " + "x".equals("x") + " " + "x".hashCode() + " " + "hello world".hashCode());
        String joined = String.join("-", "a", "b", "c");
        System.out.println(joined + " " + joined.replace('-', '+') + " " + "a,b,,c".split(",").length + " " + "abc".substring(1) + " " + "abcdef".substring(2, 4));
        char c = 'x';
        c++;
        int ci = c + 1;
        System.out.println(c + " " + ci + " " + (char) ci + " " + Character.isDigit('5') + " " + Character.toUpperCase('q'));
        Object nul = null;
        System.out.println("null concat: " + nul + " " + 1 + 2 + " " + (1 + 2) + " " + 1.5f + " " + 'c' + " " + true + " " + 10L);
        System.out.println("unicode: Привет, мир! ✓ 😀 len=" + "😀".length());
        String a = "lit";
        String b = "lit";
        System.out.println("same literal: " + (a == b) + ", built equals: " + new StringBuilder("li").append('t').toString().equals(a));
        try { "abc".charAt(5); } catch (StringIndexOutOfBoundsException e) { System.out.println("caught SIOOBE"); }
        try { Integer.parseInt("12x"); } catch (NumberFormatException e) { System.out.println("NFE " + e.getMessage()); }
    }
}
