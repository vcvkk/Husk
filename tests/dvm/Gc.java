public class Gc {
    static final class Node { final int v; Node next; int[] payload; Node(int v, Node n) { this.v = v; next = n; payload = new int[8]; payload[3] = v; } }
    static final class Tree { Tree l, r; final int v; Tree(int v) { this.v = v; } }
    static Tree build(int d) { Tree t = new Tree(d); if (d > 0) { t.l = build(d - 1); t.r = build(d - 1); } return t; }
    static long check(Tree t) { return t == null ? 0 : t.v + check(t.l) + check(t.r); }
    static Node keep;

    public static void main(String[] args) {
        Tree longLived = build(14);
        for (int i = 0; i < 1000; i++) keep = new Node(i, keep);
        long garbage = 0;
        for (int round = 0; round < 60; round++) {
            Node head = null;
            for (int i = 0; i < 20000; i++) head = new Node(i, head);
            for (Node n = head; n != null; n = n.next) garbage += n.payload[3];
            String s = "";
            for (int i = 0; i < 50; i++) s = s + i;
            garbage += s.length();
            Tree t = build(8);
            garbage += check(t);
        }
        long kept = 0;
        for (Node n = keep; n != null; n = n.next) kept += n.payload[3];
        System.out.println("garbage checksum " + garbage);
        System.out.println("kept " + kept + " tree " + check(longLived));
        System.gc();
        System.out.println("tree after gc " + check(longLived));
    }
}
