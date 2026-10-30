package dev.huskport.composehello

import android.content.Context
import android.opengl.GLES20
import android.opengl.GLSurfaceView
import android.util.Log
import java.nio.ByteBuffer
import java.nio.ByteOrder
import javax.microedition.khronos.egl.EGLConfig
import javax.microedition.khronos.opengles.GL10

/** OpenGL ES 2 through ANGLE: a triangle from a shader over a background that turns from red to blue, each frame. */
class GLTest(c: Context) : GLSurfaceView(c) {
    init {
        setEGLContextClientVersion(2)
        setRenderer(object : Renderer {
            var program = 0; var frames = 0
            val tri = ByteBuffer.allocateDirect(6 * 4).order(ByteOrder.nativeOrder()).asFloatBuffer().apply { put(floatArrayOf(0f, 0.7f, -0.7f, -0.6f, 0.7f, -0.6f)); position(0) }
            fun shader(type: Int, src: String) = GLES20.glCreateShader(type).also { GLES20.glShaderSource(it, src); GLES20.glCompileShader(it) }
            override fun onSurfaceCreated(gl: GL10?, config: EGLConfig?) {
                program = GLES20.glCreateProgram()
                GLES20.glAttachShader(program, shader(GLES20.GL_VERTEX_SHADER, "attribute vec2 p; void main() { gl_Position = vec4(p, 0.0, 1.0); }"))
                GLES20.glAttachShader(program, shader(GLES20.GL_FRAGMENT_SHADER, "precision mediump float; void main() { gl_FragColor = vec4(1.0, 0.85, 0.1, 1.0); }"))
                GLES20.glLinkProgram(program)
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
