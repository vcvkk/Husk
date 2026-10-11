/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * The framework's natives as the host app sees them: what it tells them about the app and the
 * screen, the input it hands them, and the frames it takes from them.
 *
 *   husk_app_configure(&cfg);               before the VM runs any of the framework
 *   husk_framework_register(env);           after JNI_CreateJavaVM: husk.Native, husk.Gfx
 *   husk.AppRunner.run(activity, app)       on the thread that is the app's main thread
 *   husk_app_touch(...), husk_app_text(...) from the host's UI thread
 *   husk_frame_take(...) / husk_frame_done() what the app drew, to put on screen
 */
#ifndef HUSK_FRAMEWORK_H
#define HUSK_FRAMEWORK_H
#include <jni.h>
#include <stdbool.h>
#include <stdint.h>

typedef struct husk_app_config {
    const char *apk;                 /* the app's APK */
    const char *framework_res;       /* the platform's resources (framework-res.apk), or NULL */
    const char *data;                /* its data directory (Context.getDataDir) */
    const char *external;            /* its external files directory */
    const char *package;
    int width, height;               /* the screen in pixels */
    float density;                   /* pixels per dp */
    bool night_mode;
    void (*open_url)(const char *url);
    void (*show_keyboard)(bool show, int input_type, int ime_options);
    void (*set_clipboard)(const char *text);
    char *(*get_clipboard)(void);    /* malloc'd, or NULL */
    void (*share)(const char *text);
    void (*vibrate)(int ms);
    void (*set_orientation)(int android_orientation);
    void (*exit)(void);
    bool vsync;                      /* the host calls husk_app_vsync at each display refresh */
    float refresh_rate;              /* its refreshes a second (0: 60) */
} husk_app_config;

void husk_app_configure(const husk_app_config *cfg);
bool husk_framework_register(JNIEnv *env);

/* touches: phase 0 down, 1 move, 2 up, 3 cancel; id the pointer; x, y in pixels */
void husk_app_touch(int phase, int id, float x, float y);
void husk_app_back(void);
void husk_app_text(const char *utf8);             /* typed text, committed */
void husk_app_text_delete(void);
void husk_app_text_action(void);                  /* the keyboard's return key */
void husk_app_text_hidden(void);                  /* the host's keyboard went away (the user closed it) */
void husk_app_insets(int left, int top, int right, int bottom, int keyboard);
void husk_app_vsync(void);                        /* the display refreshed (from any thread) */

/* The latest frame the app drew, when there is a new one: premultiplied RGBA rows, 4 * w bytes
 * each, locked until husk_frame_done. *any is false when the app shows nothing. */
bool husk_frame_take(const uint8_t **px, int *w, int *h, bool *any);
void husk_frame_done(void);

#endif
