package husk;

/** HUSK_FRAME_STATS: what a frame's drawing is made of (bitmaps drawn, render nodes recorded and drawn), summed for the log. */
public final class DrawStats {
    private DrawStats() {}
    public static final boolean ON = System.getenv("HUSK_FRAME_STATS") != null;
    public static long bitmaps, bitmapPx, bitmapNs, nodesRecorded, nodePx, nodesDrawn, nodeDrawNs;
    public static void bitmap(float w, float h, long ns) { bitmaps++; bitmapPx += (long) Math.abs(w * h); bitmapNs += ns; }
    public static String take(long frames) {
        double f = Math.max(1, frames);
        String s = String.format(java.util.Locale.ROOT,
            "per frame: %.1f bitmaps (%.2f Mpx, %.2f ms), %.1f render nodes recorded (%.2f Mpx of bitmap), %.1f drawn (%.2f ms)",
            bitmaps / f, bitmapPx / f / 1e6, bitmapNs / f / 1e6, nodesRecorded / f, nodePx / f / 1e6, nodesDrawn / f, nodeDrawNs / f / 1e6);
        bitmaps = bitmapPx = bitmapNs = nodesRecorded = nodePx = nodesDrawn = nodeDrawNs = 0;
        return s;
    }
}
