package dev.huskport.composehello

import android.util.Log

/** libhello.so (native/hello.cpp): what the guest linker has to get right, each check a line in the log and on screen. */
object Native {
    @JvmStatic external fun add(a: Int, b: Int): Int
    @JvmStatic external fun greet(who: String): String
    @JvmStatic external fun callback(): Int
    @JvmStatic external fun many(a: Int, b: Int, c: Int, d: Int, e: Int, f: Int, g: Int, h: Int, i: Int, j: Int, x: Float, y: Double): Long
    @JvmStatic external fun thread(): Boolean

    @JvmStatic fun cb(i: Int, f: Float, l: Long, d: Double): Int = if (i == 3 && f == 4.5f && l == 7000000000L && d == 2.25) 1234 else -2
    @JvmStatic fun fromThread(s: String) { Log.i("NativeTest", "from $s") }

    /** each check's result, or why it could not run */
    fun report(): String {
        val out = StringBuilder()
        fun check(name: String, block: () -> Any?) {
            val r = try { block().toString() } catch (t: Throwable) { "FAILED " + t }
            Log.i("NativeTest", "$name: $r")
            out.append(name).append(": ").append(r).append('\n')
        }
        try { System.loadLibrary("hello") } catch (t: Throwable) { Log.i("NativeTest", "loadLibrary: $t"); return "no native library: $t" }
        check("add") { add(2, 40) }                                         // 42: RegisterNatives
        check("greet") { greet("husk") }                                   // hello husk 3.14 42 ok caught
        check("callback") { callback() }                                  // 1234: a variadic call back into Java
        check("many") { many(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 1.5f, 2.5) }   // 385 + 150 + 2500 = 3035
        check("thread") { thread() }                                      // true
        return out.toString()
    }
}
