package android.graphics;

/**
 * An effect a RenderNode draws its content through. Only blurs are drawn (RenderNode.huskDraw): a chain's or a blur's input
 * adds its radius; the other effects are kept and drawn as their input.
 */
public final class RenderEffect {
    final float mBlurX, mBlurY;
    private RenderEffect(float bx, float by) { mBlurX = bx; mBlurY = by; }
    private static RenderEffect of(RenderEffect in) { return in == null ? new RenderEffect(0, 0) : in; }

    public static RenderEffect createBlurEffect(float rx, float ry, RenderEffect input, Shader.TileMode edge) {
        RenderEffect i = of(input);
        return new RenderEffect(i.mBlurX + rx, i.mBlurY + ry);
    }
    public static RenderEffect createBlurEffect(float rx, float ry, Shader.TileMode edge) { return new RenderEffect(rx, ry); }
    public static RenderEffect createChainEffect(RenderEffect outer, RenderEffect inner) {
        RenderEffect a = of(outer), b = of(inner);
        return new RenderEffect(a.mBlurX + b.mBlurX, a.mBlurY + b.mBlurY);
    }
    public static RenderEffect createBitmapEffect(Bitmap b) { return new RenderEffect(0, 0); }
    public static RenderEffect createBitmapEffect(Bitmap b, Rect src, Rect dst) { return new RenderEffect(0, 0); }
    public static RenderEffect createBlendModeEffect(RenderEffect dst, RenderEffect src, BlendMode mode) { return of(src); }
    public static RenderEffect createColorFilterEffect(ColorFilter f) { return new RenderEffect(0, 0); }
    public static RenderEffect createColorFilterEffect(ColorFilter f, RenderEffect input) { return of(input); }
    public static RenderEffect createOffsetEffect(float dx, float dy) { return new RenderEffect(0, 0); }
    public static RenderEffect createOffsetEffect(float dx, float dy, RenderEffect input) { return of(input); }
    public static RenderEffect createRuntimeShaderEffect(RuntimeShader s, String uniform) { return new RenderEffect(0, 0); }
    public static RenderEffect createShaderEffect(Shader s) { return new RenderEffect(0, 0); }
}
