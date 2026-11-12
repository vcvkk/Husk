package android.graphics;
/** A nine-patch: stretchable regions from its chunk (res_png9 compiled form). */
public class NinePatch {
    private final Bitmap mBitmap; final int[] xDivs, yDivs, colors; public final Rect padding = new Rect();
    /* a patch's colour hint, as Res_png_9patch has it: its pixels stretched, nothing, or one colour drawn instead */
    private static final int NO_COLOR = 1, TRANSPARENT_COLOR = 0;
    private Paint mFill;
    public NinePatch(Bitmap b, byte[] chunk) { this(b, chunk, null); }
    public NinePatch(Bitmap b, byte[] chunk, String src) {
        mBitmap = b;
        int[][] d = parse(chunk, padding);
        xDivs = d[0]; yDivs = d[1]; colors = d[2];
        dump(b);
    }
    /* HUSK_NINEPATCH_DUMP=DIR: each nine-patch's bitmap as DIR/ninepatch-N.png, its divs and colours to the log */
    private static final String DUMP = System.getenv("HUSK_NINEPATCH_DUMP");
    private static int sDumped;
    private void dump(Bitmap b) {
        if (DUMP == null || b == null || sDumped >= 40) return;
        int n = sDumped++;
        android.util.Log.i("HuskNinePatch", n + ": " + b.getWidth() + "x" + b.getHeight() + " x " + java.util.Arrays.toString(xDivs) + " y " + java.util.Arrays.toString(yDivs)
            + " colors " + java.util.Arrays.toString(colors) + " padding " + padding);
        try (java.io.FileOutputStream o = new java.io.FileOutputStream(DUMP + "/ninepatch-" + n + ".png")) { b.compress(Bitmap.CompressFormat.PNG, 100, o); }
        catch (Exception e) { android.util.Log.w("HuskNinePatch", "dump: " + e); }
    }
    public static boolean isNinePatchChunk(byte[] c) { return c != null && c.length >= 32 && c[0] != 0; }
    /** The serialized Res_png_9patch: wasDeserialized, numXDivs, numYDivs, numColors, 2 x int offsets, padding l r t b, color offset,
     *  then the divs and the colours (a patch each, row by row). */
    static int[][] parse(byte[] c, Rect pad) {
        if (c == null || c.length < 32) return new int[][] { new int[0], new int[0], new int[0] };
        java.nio.ByteBuffer b = java.nio.ByteBuffer.wrap(c).order(java.nio.ByteOrder.nativeOrder());
        b.get(); int nx = b.get() & 255, ny = b.get() & 255, nc = b.get() & 255;
        b.getInt(); b.getInt();
        int pl = b.getInt(), pr = b.getInt(), pt = b.getInt(), pb = b.getInt();
        if (pad != null) pad.set(pl, pt, pr, pb);
        b.getInt();
        int[] xs = new int[nx], ys = new int[ny];
        for (int i = 0; i < nx && b.remaining() >= 4; i++) xs[i] = b.getInt();
        for (int i = 0; i < ny && b.remaining() >= 4; i++) ys[i] = b.getInt();
        int[] cs = new int[b.remaining() >= nc * 4 ? nc : 0];
        for (int i = 0; i < cs.length; i++) cs[i] = b.getInt();
        return new int[][] { xs, ys, cs };
    }
    public Bitmap getBitmap() { return mBitmap; }
    public int getWidth() { return mBitmap.getWidth(); }
    public int getHeight() { return mBitmap.getHeight(); }
    public final boolean hasAlpha() { return true; }
    public void setPaint(Paint p) {}
    public void draw(Canvas c, RectF dst) { draw(c, dst, null); }
    public void draw(Canvas c, Rect dst) { draw(c, new RectF(dst), null); }
    public void draw(Canvas c, Rect dst, Paint p) { draw(c, new RectF(dst), p); }
    public void draw(Canvas c, RectF dst, Paint p) {
        int w = mBitmap.getWidth(), h = mBitmap.getHeight();
        float[] sx = segs(xDivs, w), sy = segs(yDivs, h);
        float[] dx = place(xDivs, sx, w, dst.left, dst.width()), dy = place(yDivs, sy, h, dst.top, dst.height());
        Rect s = new Rect(); RectF d = new RectF();
        int cols = sx.length - 1;
        boolean hints = colors.length == cols * (sy.length - 1);
        for (int j = 0; j + 1 < sy.length; j++) for (int i = 0; i + 1 < sx.length; i++) {
            s.set((int) sx[i], (int) sy[j], (int) sx[i + 1], (int) sy[j + 1]);
            d.set(dx[i], dy[j], dx[i + 1], dy[j + 1]);
            if (s.width() <= 0 || s.height() <= 0 || d.width() <= 0 || d.height() <= 0) continue;
            int hint = hints ? colors[j * cols + i] : NO_COLOR;
            if (hint == NO_COLOR) c.drawBitmap(mBitmap, s, d, p);
            else if (hint != TRANSPARENT_COLOR) {
                /* a patch of one colour: drawn as that colour (Telegram's generated glass buttons leave its pixels empty) */
                if (mFill == null) mFill = new Paint();
                mFill.setColor(hint);
                if (p != null) mFill.setAlpha(mFill.getAlpha() * p.getAlpha() / 255);
                mFill.setColorFilter(p != null ? p.getColorFilter() : null);
                c.drawRect(d, mFill);
            }
        }
    }
    private static float[] segs(int[] divs, int size) {
        float[] o = new float[divs.length + 2];
        o[0] = 0; for (int i = 0; i < divs.length; i++) o[i + 1] = Math.min(size, Math.max(0, divs[i])); o[o.length - 1] = size;
        return o;
    }
    /** The destination edges: fixed segments keep their size, stretchable (odd) ones share what is left. */
    private static float[] place(int[] divs, float[] s, int size, float start, float len) {
        float fixed = 0, stretch = 0;
        for (int i = 0; i + 1 < s.length; i++) { float l = s[i + 1] - s[i]; if ((i & 1) == 1) stretch += l; else fixed += l; }
        float k = stretch > 0 ? Math.max(0, len - fixed) / stretch : 0, shrink = fixed > len && fixed > 0 ? len / fixed : 1;
        float[] o = new float[s.length];
        o[0] = start;
        for (int i = 0; i + 1 < s.length; i++) { float l = s[i + 1] - s[i]; o[i + 1] = o[i] + ((i & 1) == 1 ? l * k : l * shrink); }
        return o;
    }
    // ---- generated by tools/compat/fillmembers.py: the platform's members this class does not write (signatures only)
    public long mNativeChunk;
    public int getDensity() { return 0; }
    public java.lang.String getName() { return null; }
    public android.graphics.Paint getPaint() { return null; }
    public android.graphics.Region getTransparentRegion(android.graphics.Rect p0) { return null; }
    // ---- end of generated members
    // ---- generated by tools/compat/genstubs.py: the platform's nested classes this class does not write
    public static class InsetStruct {
        private final java.util.HashMap<String, Object> huskProps = new java.util.HashMap<>();
        public android.graphics.Rect opticalRect;
        public float outlineAlpha;
        public float outlineRadius;
        public android.graphics.Rect outlineRect;
        public static android.graphics.Rect scaleInsets(int p0, int p1, int p2, int p3, float p4) { return null; }
        protected InsetStruct() {}
    }
    // ---- end of generated nested classes
}
