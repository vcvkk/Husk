package husk;

/**
 * The code a software canvas tells it is a hardware one. The window is drawn in software, and where a canvas is not hardware
 * accelerated Android's apps choose their software paths; but some draw only on a hardware canvas (Android's always is from
 * 12 on), and draw nothing on another. Their RenderNodes are drawn here as bitmaps (RenderNode.huskDraw).
 */
public final class HwCanvas {
    private HwCanvas() {}
    /* exteraGram's and Telegram's glass backgrounds (the tab bar's, the action bars'): a RenderNode source with no fallback */
    private static final String[] PREFIXES = { "org.telegram.ui.Components.blur3." };

    public static boolean wants(Class<?> caller) {
        if (caller == null) return false;
        String n = caller.getName();
        for (String p : PREFIXES) if (n.startsWith(p)) return true;
        return false;
    }
}
