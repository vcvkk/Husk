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
        long[] b = new long[9];
        Gfx.blitStats(b);
        s += String.format(java.util.Locale.ROOT,
            "; Husk drew %.1f of them (%.2f Mpx 1:1, %.2f Mpx scaled, %.2f ms); to CoreGraphics for paint %.1f, layer %.1f, clip %.1f, turned %.1f, shrunk %.1f",
            b[0] / f, b[1] / f / 1e6, b[2] / f / 1e6, b[3] / f / 1e6, b[4] / f, b[5] / f, b[6] / f, b[7] / f, b[8] / f);
        return s;
    }
}
