package dev.huskport.composehello

import android.content.Context
import android.graphics.Bitmap
import android.graphics.Color
import android.opengl.GLES20
import android.opengl.GLUtils
import android.opengl.GLSurfaceView
import android.util.Log
import java.nio.ByteBuffer
import java.nio.ByteOrder
import javax.microedition.khronos.egl.EGLConfig
import javax.microedition.khronos.opengles.GL10

/** OpenGL ES 2 through ANGLE: a triangle from a shader, textured from a bitmap (GLUtils), over a background that turns from red to blue, each frame. */
class GLTest(c: Context) : GLSurfaceView(c) {
    init {
        setEGLContextClientVersion(2)
        setRenderer(object : Renderer {
            var program = 0; var frames = 0
            val tri = ByteBuffer.allocateDirect(6 * 4).order(ByteOrder.nativeOrder()).asFloatBuffer().apply { put(floatArrayOf(0f, 0.7f, -0.7f, -0.6f, 0.7f, -0.6f)); position(0) }
            fun shader(type: Int, src: String) = GLES20.glCreateShader(type).also { GLES20.glShaderSource(it, src); GLES20.glCompileShader(it) }
            override fun onSurfaceCreated(gl: GL10?, config: EGLConfig?) {
                program = GLES20.glCreateProgram()
                GLES20.glAttachShader(program, shader(GLES20.GL_VERTEX_SHADER, "attribute vec2 p; varying vec2 uv; void main() { uv = p * 0.5 + 0.5; gl_Position = vec4(p, 0.0, 1.0); }"))
                GLES20.glAttachShader(program, shader(GLES20.GL_FRAGMENT_SHADER, "precision mediump float; varying vec2 uv; uniform sampler2D t; void main() { gl_FragColor = texture2D(t, uv); }"))
                GLES20.glLinkProgram(program)
                // a bitmap of yellow rows over green ones (GL puts its first row at the bottom: green on the triangle's tip) uploaded through GLUtils.texImage2D (glTexImage2D's ten arguments)
                val bm = Bitmap.createBitmap(4, 4, Bitmap.Config.ARGB_8888)
                for (y in 0 until 4) for (x in 0 until 4) bm.setPixel(x, y, if (y < 2) Color.rgb(255, 217, 26) else Color.rgb(40, 200, 80))
                val tex = IntArray(1); GLES20.glGenTextures(1, tex, 0)
                GLES20.glBindTexture(GLES20.GL_TEXTURE_2D, tex[0])
                GLES20.glTexParameteri(GLES20.GL_TEXTURE_2D, GLES20.GL_TEXTURE_MIN_FILTER, GLES20.GL_NEAREST)
                GLES20.glTexParameteri(GLES20.GL_TEXTURE_2D, GLES20.GL_TEXTURE_MAG_FILTER, GLES20.GL_NEAREST)
                GLUtils.texImage2D(GLES20.GL_TEXTURE_2D, 0, bm, 0)
                Log.i("GLTest", "texture: error 0x" + Integer.toHexString(GLES20.glGetError()) + ", link log '" + GLES20.glGetProgramInfoLog(program) + "'")
                Log.i("GLTest", "GL: " + GLES20.glGetString(GLES20.GL_RENDERER) + " / " + GLES20.glGetString(GLES20.GL_VERSION))
            }
            override fun onSurfaceChanged(gl: GL10?, w: Int, h: Int) { GLES20.glViewport(0, 0, w, h); Log.i("GLTest", "surface ${w}x$h") }
            override fun onDrawFrame(gl: GL10?) {
                val t = (frames++ % 120) / 120f
                GLES20.glClearColor(1f - t, 0.2f, t, 1f)
                GLES20.glClear(GLES20.GL_COLOR_BUFFER_BIT)
                GLES20.glUseProgram(program)
                val p = GLES20.glGetAttribLocation(program, "p")
                GLES20.glEnableVertexAttribArray(p)
                GLES20.glVertexAttribPointer(p, 2, GLES20.GL_FLOAT, false, 0, tri)
                GLES20.glDrawArrays(GLES20.GL_TRIANGLES, 0, 3)
                if (frames % 60 == 1) Log.i("GLTest", "frame $frames")
            }
        })
    }
}
