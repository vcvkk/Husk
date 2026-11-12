package husk;

import android.graphics.Canvas;
import android.graphics.Paint;

/**
 * HUSK_DRAW_TRACE=PREFIX[:COUNT[:AFTER]]: the canvas calls that code in classes whose names start with PREFIX makes (directly or
 * a few calls down, through a drawable), to the log with the clip, the matrix and the paint: to see what an app draws for
 * something that does not show. The first COUNT (300) of them, from AFTER seconds (0) after the first drawing.
 */
public final class DrawTrace {
    private DrawTrace() {}
    private static final String SPEC = System.getenv("HUSK_DRAW_TRACE");
    public static final boolean ON = SPEC != null && !SPEC.isEmpty();
    private static final String[] PARTS = ON ? SPEC.split(":") : new String[0];
    private static int sLeft = !ON ? 0 : PARTS.length > 1 ? Integer.parseInt(PARTS[1]) : 300;
    private static final long FROM = System.nanoTime() + (PARTS.length > 2 ? (long) (Double.parseDouble(PARTS[2]) * 1e9) : 0);

    public static void op(Canvas c, String op, Paint p) {
        if (sLeft <= 0 || System.nanoTime() < FROM) return;
        StackTraceElement[] st = new Throwable().getStackTrace();
        String hit = null;
        for (int i = 2; i < Math.min(st.length, 9) && hit == null; i++)
            if (st[i].getClassName().startsWith(PARTS[0])) hit = st[i].getClassName() + "." + st[i].getMethodName() + ":" + st[i].getLineNumber();
        if (hit == null) return;
        sLeft--;
        android.util.Log.i("HuskDraw", op + " clip " + c.getClipBounds().toShortString() + " matrix " + c.getMatrix().toShortString()
            + " paint " + paint(p) + " canvas " + c.getWidth() + "x" + c.getHeight() + " from " + hit);
    }

    static String paint(Paint p) {
        if (p == null) return "null";
        StringBuilder s = new StringBuilder("alpha ").append(p.getAlpha()).append(" color ").append(Integer.toHexString(p.getColor()));
        if (p.getShader() != null) s.append(" shader ").append(p.getShader());
        if (p.getColorFilter() != null) s.append(" filter ").append(p.getColorFilter().getClass().getSimpleName());
        if (p.getXfermode() != null) s.append(" xfer ").append(p.getXfermode());
        if (p.getStyle() != Paint.Style.FILL) s.append(" style ").append(p.getStyle());
        return s.toString();
    }

    public static String bounds(android.graphics.Path path) {
        android.graphics.RectF r = new android.graphics.RectF();
        path.computeBounds(r, true);
        return "[" + r.left + "," + r.top + "][" + r.right + "," + r.bottom + "]";
    }

    public static String bitmap(android.graphics.Bitmap b) {
        return b == null ? "null" : "Bitmap " + b.getWidth() + "x" + b.getHeight() + " " + b.getConfig() + (b.isRecycled() ? " recycled" : "")
            + " native " + (b.huskNative() != 0) + " px@center " + (b.isRecycled() || b.getWidth() == 0 ? "-" : Integer.toHexString(b.getPixel(b.getWidth() / 2, b.getHeight() / 2)));
    }
}
