// SPDX-License-Identifier: GPL-2.0-or-later
// libhello.so: the test app's native library, for husk-port's guest linker (port/native) under ART. Each check is one a real
// app's library depends on: JNI_OnLoad and RegisterNatives, Java_ functions found by name, a variadic call back into Java with
// int, float, long and double arguments, a native method with stack arguments, printf-style formatting, libc++ (strings,
// exceptions) and a thread of its own that attaches to the VM and calls Java.
#include <jni.h>
#include <pthread.h>
#include <stdexcept>
#include <stdio.h>
#include <string>

static JavaVM *g_vm;
static jclass g_cls;

static jint add(JNIEnv *, jclass, jint a, jint b) { return a + b; }

extern "C" JNIEXPORT jstring JNICALL Java_dev_huskport_composehello_Native_greet(JNIEnv *env, jclass, jstring who)
{
    const char *w = env->GetStringUTFChars(who, nullptr);
    std::string s = std::string("hello ") + w;
    env->ReleaseStringUTFChars(who, w);
    char buf[128];
    snprintf(buf, sizeof buf, " %.2f %d %s", 3.14159, 42, "ok");
    s += buf;
    try { throw std::runtime_error("caught"); } catch (const std::exception &e) { s += " "; s += e.what(); }
    return env->NewStringUTF(s.c_str());
}

// Native.cb(int, float, long, double) -> int, called variadically: CallStaticIntMethod(env, cls, mid, ...)
extern "C" JNIEXPORT jint JNICALL Java_dev_huskport_composehello_Native_callback(JNIEnv *env, jclass cls)
{
    jmethodID m = env->GetStaticMethodID(cls, "cb", "(IFJD)I");
    if (!m) return -1;
    return env->CallStaticIntMethod(cls, m, (jint)3, 4.5f, (jlong)7000000000LL, 2.25);
}

// stack arguments: twelve after env and the class
extern "C" JNIEXPORT jlong JNICALL Java_dev_huskport_composehello_Native_many(JNIEnv *, jclass, jint a, jint b, jint c, jint d,
    jint e, jint f, jint g, jint h, jint i, jint j, jfloat x, jdouble y)
{
    return (jlong)a + b * 2 + c * 3 + d * 4 + e * 5 + f * 6 + g * 7 + h * 8 + i * 9 + j * 10 + (jlong)(x * 100) + (jlong)(y * 1000);
}

static void *worker(void *arg)
{
    JNIEnv *env = nullptr;
    if (g_vm->AttachCurrentThread(&env, nullptr) != JNI_OK) return nullptr;
    jmethodID m = env->GetStaticMethodID(g_cls, "fromThread", "(Ljava/lang/String;)V");
    if (m) env->CallStaticVoidMethod(g_cls, m, env->NewStringUTF("a guest thread"));
    g_vm->DetachCurrentThread();
    *(int *)arg = 1;
    return nullptr;
}
extern "C" JNIEXPORT jboolean JNICALL Java_dev_huskport_composehello_Native_thread(JNIEnv *, jclass)
{
    int done = 0;
    pthread_t t;
    if (pthread_create(&t, nullptr, worker, &done) != 0) return JNI_FALSE;
    pthread_join(t, nullptr);
    return done ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jint JNI_OnLoad(JavaVM *vm, void *)
{
    g_vm = vm;
    JNIEnv *env = nullptr;
    if (vm->GetEnv((void **)&env, JNI_VERSION_1_6) != JNI_OK) return -1;
    jclass c = env->FindClass("dev/huskport/composehello/Native");
    if (!c) return -1;
    g_cls = (jclass)env->NewGlobalRef(c);
    static const JNINativeMethod m[] = { { "add", "(II)I", (void *)add } };
    if (env->RegisterNatives(c, m, 1) != 0) return -1;
    return JNI_VERSION_1_6;
}
