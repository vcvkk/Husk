package android.opengl;

import java.util.ArrayList;
import javax.microedition.khronos.egl.EGL10;
import javax.microedition.khronos.egl.EGLConfig;
import javax.microedition.khronos.egl.EGLContext;
import javax.microedition.khronos.egl.EGLDisplay;
import javax.microedition.khronos.egl.EGLSurface;
import javax.microedition.khronos.opengles.GL10;

/**
 * The renderer runs on a GL thread of its own, as on Android: an EGL context through ANGLE (husk.EGLNative, jni/gl.c), a window
 * surface the view's size, onSurfaceCreated, onSurfaceChanged and onDrawFrame, and each frame read back into the view (HuskEGL's
 * eglSwapBuffers) where the window's views are drawn.
 */
public class GLSurfaceView extends android.view.SurfaceView implements android.view.SurfaceHolder.Callback2 {
    public static final int RENDERMODE_WHEN_DIRTY = 0, RENDERMODE_CONTINUOUSLY = 1, DEBUG_CHECK_GL_ERROR = 1, DEBUG_LOG_GL_CALLS = 2;
    public interface Renderer { void onSurfaceCreated(GL10 gl, EGLConfig config); void onSurfaceChanged(GL10 gl, int w, int h); void onDrawFrame(GL10 gl); }
    public interface EGLConfigChooser { EGLConfig chooseConfig(EGL10 egl, EGLDisplay display); }
    public interface EGLContextFactory { EGLContext createContext(EGL10 egl, EGLDisplay d, EGLConfig c); void destroyContext(EGL10 egl, EGLDisplay d, EGLContext c); }
    public interface EGLWindowSurfaceFactory { EGLSurface createWindowSurface(EGL10 egl, EGLDisplay d, EGLConfig c, Object w); void destroySurface(EGL10 egl, EGLDisplay d, EGLSurface s); }
    public interface GLWrapper { javax.microedition.khronos.opengles.GL wrap(javax.microedition.khronos.opengles.GL gl); }

    private Renderer renderer;
    private int version = 1, renderMode = RENDERMODE_CONTINUOUSLY;
    private boolean versionSet;
    private EGLContextFactory contextFactory;
    private volatile boolean dirty = true, started, paused;
    private final ArrayList<Runnable> queue = new ArrayList<>();

    public GLSurfaceView(android.content.Context c) { this(c, null); }
    public GLSurfaceView(android.content.Context c, android.util.AttributeSet a) { super(c, a); getHolder().addCallback(this); }
    public void surfaceCreated(android.view.SurfaceHolder h) { start(); }
    /** A new size for the surface (the view was laid out again: bars hidden, rotated): the GL thread tells the renderer. */
    public void surfaceChanged(android.view.SurfaceHolder h, int format, int w, int ht) { if (w > 0 && ht > 0) { resized = ((long) w << 32) | ht; requestRender(); } }
    private volatile long resized;
    /** @hide Husk's GL thread: the size to give onSurfaceChanged, once (0: none new). */
    public final long huskTakeResize() { long r = resized; resized = 0; return r; }
    public void surfaceDestroyed(android.view.SurfaceHolder h) {}
    public void surfaceRedrawNeeded(android.view.SurfaceHolder h) { requestRender(); }
    public void surfaceRedrawNeededAsync(android.view.SurfaceHolder h, Runnable done) { requestRender(); done.run(); }
    public void setRenderer(Renderer r) { renderer = r; if (isAttachedToWindow()) start(); }
    public void setEGLContextClientVersion(int v) { version = v; versionSet = true; }
    public void setEGLConfigChooser(EGLConfigChooser c) {}
    public void setEGLConfigChooser(boolean depth) {}
    public void setEGLConfigChooser(int r, int g, int b, int a, int depth, int stencil) {}
    public void setEGLContextFactory(EGLContextFactory f) { contextFactory = f; }
    public void setEGLWindowSurfaceFactory(EGLWindowSurfaceFactory f) {}
    public void setGLWrapper(GLWrapper w) {}
    public void setDebugFlags(int f) {}
    public int getDebugFlags() { return 0; }
    public void setPreserveEGLContextOnPause(boolean b) {}
    public boolean getPreserveEGLContextOnPause() { return true; }
    public void setRenderMode(int m) { renderMode = m; dirty = true; }
    public int getRenderMode() { return renderMode; }
    public void requestRender() { dirty = true; synchronized (signal) { signal.notifyAll(); } }
    private final Object signal = new Object();
    private volatile boolean detached;
    public void onPause() { paused = true; }
    public void onResume() { paused = false; dirty = true; }
    public void queueEvent(Runnable r) { synchronized (queue) { queue.add(r); } requestRender(); }
    @Override protected void onDetachedFromWindow() { detached = true; requestRender(); super.onDetachedFromWindow(); }
    @Override protected void onAttachedToWindow() { super.onAttachedToWindow(); start(); }
    private void start() {
        if (renderer == null || started) return;
        started = true;
        // an app with its own context factory asks for its GL ES version there (EGL_CONTEXT_CLIENT_VERSION), not here
        if (!versionSet && contextFactory != null) {
            try {
                EGL10 egl = (EGL10) EGLContext.getEGL();
                EGLDisplay d = egl.eglGetDisplay(EGL10.EGL_DEFAULT_DISPLAY);
                EGLConfig[] cfg = new EGLConfig[1];
                egl.eglChooseConfig(d, null, cfg, 1, new int[1]);
                EGLContext probe = contextFactory.createContext(egl, d, cfg[0]);
                version = Math.max(1, EGLContext.huskRequestedVersion());
                // made off the render thread, that is a real context of its own: the render thread makes the one the app draws with
                if (probe != null && probe != EGL10.EGL_NO_CONTEXT) try { contextFactory.destroyContext(egl, d, probe); } catch (RuntimeException e) { }
            } catch (RuntimeException e) {
                android.util.Log.w("GLSurfaceView", "the app's EGLContextFactory threw: " + e);
                version = 2;
            }
        }
        detached = false;
        new GLThread().start();
    }

    /** The GL thread: its context, the surface at the view's size (made again when that changes), the frames. */
    private final class GLThread extends Thread {
        GLThread() { super("GLThread " + Integer.toHexString(System.identityHashCode(GLSurfaceView.this))); }
        @Override public void run() {
            EGL10 egl = (EGL10) EGLContext.getEGL();
            EGLDisplay d = egl.eglGetDisplay(EGL10.EGL_DEFAULT_DISPLAY);
            egl.eglInitialize(d, new int[2]);
            EGLConfig[] cfg = new EGLConfig[1];
            egl.eglChooseConfig(d, null, cfg, 1, new int[1]);
            int[] attr = { 0x3098, Math.max(1, version), EGL10.EGL_NONE };
            EGLContext ctx = null;
            try { ctx = contextFactory != null ? contextFactory.createContext(egl, d, cfg[0]) : egl.eglCreateContext(d, cfg[0], EGL10.EGL_NO_CONTEXT, attr); }
            catch (RuntimeException e) { android.util.Log.w("GLSurfaceView", "the app's EGLContextFactory threw: " + e); }
            if (ctx == null || ctx == EGL10.EGL_NO_CONTEXT) { android.util.Log.w("GLSurfaceView", "no GL ES " + version + " context"); return; }
            GL10 gl = (GL10) ctx.getGL();
            EGLSurface surf = null;
            int lw = -1, lh = -1;
            boolean created = false;
            long frame = (long) (1e9 / husk.Native.refreshRate());
            while (!detached) {
                long t0 = System.nanoTime();
                huskTakeResize();
                int[] sz = huskSurfaceSize();
                if (sz[0] != lw || sz[1] != lh) {
                    if (surf != null) egl.eglDestroySurface(d, surf);
                    surf = egl.eglCreateWindowSurface(d, cfg[0], getHolder(), null);
                    if (surf == EGL10.EGL_NO_SURFACE || !egl.eglMakeCurrent(d, surf, surf, ctx)) { android.util.Log.w("GLSurfaceView", "no surface " + sz[0] + "x" + sz[1]); break; }
                    if (!created) { renderer.onSurfaceCreated(gl, cfg[0]); created = true; }
                    renderer.onSurfaceChanged(gl, sz[0], sz[1]);
                    lw = sz[0]; lh = sz[1]; dirty = true;
                }
                if (huskBeginFrame()) { renderer.onDrawFrame(gl); egl.eglSwapBuffers(d, surf); }
                long left = renderMode == RENDERMODE_CONTINUOUSLY && !paused ? frame - (System.nanoTime() - t0) : 100_000_000L;
                if (left > 0) synchronized (signal) {
                    if (!dirty || renderMode == RENDERMODE_CONTINUOUSLY) try { signal.wait(left / 1_000_000L, (int) (left % 1_000_000L)); } catch (InterruptedException e) { break; }
                }
            }
            egl.eglMakeCurrent(d, EGL10.EGL_NO_SURFACE, EGL10.EGL_NO_SURFACE, EGL10.EGL_NO_CONTEXT);
            if (surf != null) egl.eglDestroySurface(d, surf);
            if (contextFactory != null) try { contextFactory.destroyContext(egl, d, ctx); } catch (RuntimeException e) {}
            else egl.eglDestroyContext(d, ctx);
            started = false;
        }
    }

    private static HuskGL10 sGL;
    /** @hide The GL10 object EGLContext.getGL() hands out. */
    public static synchronized javax.microedition.khronos.opengles.GL huskGL() { if (sGL == null) sGL = new HuskGL10(); return sGL; }

    /** Husk's GL thread, each frame: queued events, then whether to draw. */
    public final boolean huskBeginFrame() {
        ArrayList<Runnable> run;
        synchronized (queue) { run = new ArrayList<>(queue); queue.clear(); }
        for (Runnable r : run) r.run();
        if (paused) return false;
        if (renderMode == RENDERMODE_CONTINUOUSLY) return true;
        boolean d = dirty; dirty = false; return d;
    }
}
