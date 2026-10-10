/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * husk.Sqlite on JNI: the system's sqlite3, for android.database.sqlite. Handles are sqlite3 * and
 * sqlite3_stmt * pointers. Text goes as UTF-16, as Java strings have it.
 */
#include <jni.h>
#include <sqlite3.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "shim.h"

#define DB(h) ((sqlite3 *)(intptr_t)(h))
#define ST(h) ((sqlite3_stmt *)(intptr_t)(h))

/* android.database.sqlite.SQLiteDatabase's flags */
enum { OPEN_READONLY = 0x1, CREATE_IF_NECESSARY = 0x10000000 };

static char g_open_error[256] = "unknown error";   /* errmsg(0): why the last open failed */

static jsize len16(const void *s)
{
    const uint16_t *p = s;
    jsize n = 0;
    while (p[n]) n++;
    return n;
}

static jstring str16(JNIEnv *env, const void *s, int bytes)
{
    return s ? (*env)->NewString(env, s, bytes / 2) : NULL;
}

static jlong Q_open(JNIEnv *env, jclass c, jstring path, jint flags)
{
    (void)c;
    const char *p = (*env)->GetStringUTFChars(env, path, NULL);
    int f = (flags & OPEN_READONLY) ? SQLITE_OPEN_READONLY : SQLITE_OPEN_READWRITE | ((flags & CREATE_IF_NECESSARY) ? SQLITE_OPEN_CREATE : 0);
    sqlite3 *db = NULL;
    int rc = sqlite3_open_v2(p, &db, f | SQLITE_OPEN_FULLMUTEX, NULL);
    (*env)->ReleaseStringUTFChars(env, path, p);
    if (rc != SQLITE_OK) {
        strncpy(g_open_error, db ? sqlite3_errmsg(db) : sqlite3_errstr(rc), sizeof g_open_error - 1);
        sqlite3_close_v2(db);
        return 0;
    }
    sqlite3_extended_result_codes(db, 1);
    sqlite3_busy_timeout(db, 2500);
    return (jlong)(intptr_t)db;
}
static void Q_close(JNIEnv *env, jclass c, jlong db) { (void)env; (void)c; sqlite3_close_v2(DB(db)); }
static jstring Q_errmsg(JNIEnv *env, jclass c, jlong db)
{
    (void)c;
    if (!db) return (*env)->NewStringUTF(env, g_open_error);
    const void *m = sqlite3_errmsg16(DB(db));
    return m ? (*env)->NewString(env, m, len16(m)) : NULL;
}
static jint Q_errcode(JNIEnv *env, jclass c, jlong db) { (void)env; (void)c; return sqlite3_extended_errcode(DB(db)); }
static jint Q_exec(JNIEnv *env, jclass c, jlong db, jstring sql)
{
    (void)c;
    const char *s = (*env)->GetStringUTFChars(env, sql, NULL);
    int rc = sqlite3_exec(DB(db), s, NULL, NULL, NULL);
    (*env)->ReleaseStringUTFChars(env, sql, s);
    return rc;
}
static jint Q_changes(JNIEnv *env, jclass c, jlong db) { (void)env; (void)c; return sqlite3_changes(DB(db)); }
static jlong Q_lastInsertRowid(JNIEnv *env, jclass c, jlong db) { (void)env; (void)c; return sqlite3_last_insert_rowid(DB(db)); }
static jint Q_autocommit(JNIEnv *env, jclass c, jlong db) { (void)env; (void)c; return sqlite3_get_autocommit(DB(db)); }
static void Q_busyTimeout(JNIEnv *env, jclass c, jlong db, jint ms) { (void)env; (void)c; sqlite3_busy_timeout(DB(db), ms); }

static jlong Q_prepare(JNIEnv *env, jclass c, jlong db, jstring sql)
{
    (void)c;
    const jchar *s = (*env)->GetStringChars(env, sql, NULL);
    jsize n = (*env)->GetStringLength(env, sql);
    sqlite3_stmt *st = NULL;
    int rc = sqlite3_prepare16_v2(DB(db), s, n * 2, &st, NULL);
    (*env)->ReleaseStringChars(env, sql, s);
    if (rc != SQLITE_OK) { sqlite3_finalize(st); return 0; }
    return (jlong)(intptr_t)st;
}
static void Q_finalize(JNIEnv *env, jclass c, jlong st) { (void)env; (void)c; sqlite3_finalize(ST(st)); }
static jint Q_reset(JNIEnv *env, jclass c, jlong st) { (void)env; (void)c; return sqlite3_reset(ST(st)); }
static void Q_clearBindings(JNIEnv *env, jclass c, jlong st) { (void)env; (void)c; sqlite3_clear_bindings(ST(st)); }
static jint Q_paramCount(JNIEnv *env, jclass c, jlong st) { (void)env; (void)c; return sqlite3_bind_parameter_count(ST(st)); }
static jint Q_readOnly(JNIEnv *env, jclass c, jlong st) { (void)env; (void)c; return sqlite3_stmt_readonly(ST(st)); }
static jint Q_bindNull(JNIEnv *env, jclass c, jlong st, jint i) { (void)env; (void)c; return sqlite3_bind_null(ST(st), i); }
static jint Q_bindLong(JNIEnv *env, jclass c, jlong st, jint i, jlong v) { (void)env; (void)c; return sqlite3_bind_int64(ST(st), i, v); }
static jint Q_bindDouble(JNIEnv *env, jclass c, jlong st, jint i, jdouble v) { (void)env; (void)c; return sqlite3_bind_double(ST(st), i, v); }
static jint Q_bindString(JNIEnv *env, jclass c, jlong st, jint i, jstring v)
{
    (void)c;
    const jchar *s = (*env)->GetStringChars(env, v, NULL);
    int rc = sqlite3_bind_text16(ST(st), i, s, (*env)->GetStringLength(env, v) * 2, SQLITE_TRANSIENT);
    (*env)->ReleaseStringChars(env, v, s);
    return rc;
}
static jint Q_bindBlob(JNIEnv *env, jclass c, jlong st, jint i, jbyteArray v)
{
    (void)c;
    jsize n = (*env)->GetArrayLength(env, v);
    jbyte *b = (*env)->GetByteArrayElements(env, v, NULL);
    int rc = sqlite3_bind_blob(ST(st), i, b, n, SQLITE_TRANSIENT);
    (*env)->ReleaseByteArrayElements(env, v, b, JNI_ABORT);
    return rc;
}
static jint Q_step(JNIEnv *env, jclass c, jlong st) { (void)env; (void)c; return sqlite3_step(ST(st)); }
static jint Q_columnCount(JNIEnv *env, jclass c, jlong st) { (void)env; (void)c; return sqlite3_column_count(ST(st)); }
static jstring Q_columnName(JNIEnv *env, jclass c, jlong st, jint i)
{
    (void)c;
    const void *s = sqlite3_column_name16(ST(st), i);
    return s ? (*env)->NewString(env, s, len16(s)) : NULL;
}
static jint Q_columnType(JNIEnv *env, jclass c, jlong st, jint i) { (void)env; (void)c; return sqlite3_column_type(ST(st), i); }
static jlong Q_columnLong(JNIEnv *env, jclass c, jlong st, jint i) { (void)env; (void)c; return sqlite3_column_int64(ST(st), i); }
static jdouble Q_columnDouble(JNIEnv *env, jclass c, jlong st, jint i) { (void)env; (void)c; return sqlite3_column_double(ST(st), i); }
static jstring Q_columnText(JNIEnv *env, jclass c, jlong st, jint i)
{
    (void)c;
    const void *s = sqlite3_column_text16(ST(st), i);
    return str16(env, s, sqlite3_column_bytes16(ST(st), i));
}
static jbyteArray Q_columnBlob(JNIEnv *env, jclass c, jlong st, jint i)
{
    (void)c;
    const void *b = sqlite3_column_blob(ST(st), i);
    int n = sqlite3_column_bytes(ST(st), i);
    if (!b && sqlite3_column_type(ST(st), i) == SQLITE_NULL) return NULL;
    jbyteArray a = (*env)->NewByteArray(env, n);
    if (a && n) (*env)->SetByteArrayRegion(env, a, 0, n, b);
    return a;
}
static jstring Q_version(JNIEnv *env, jclass c) { (void)c; return (*env)->NewStringUTF(env, sqlite3_libversion()); }

static const JNINativeMethod k_sqlite[] = {
    { "open", "(Ljava/lang/String;I)J", (void *)Q_open },
    { "close", "(J)V", (void *)Q_close },
    { "errmsg", "(J)Ljava/lang/String;", (void *)Q_errmsg },
    { "errcode", "(J)I", (void *)Q_errcode },
    { "exec", "(JLjava/lang/String;)I", (void *)Q_exec },
    { "changes", "(J)I", (void *)Q_changes },
    { "lastInsertRowid", "(J)J", (void *)Q_lastInsertRowid },
    { "autocommit", "(J)I", (void *)Q_autocommit },
    { "busyTimeout", "(JI)V", (void *)Q_busyTimeout },
    { "prepare", "(JLjava/lang/String;)J", (void *)Q_prepare },
    { "finalizeStatement", "(J)V", (void *)Q_finalize },
    { "reset", "(J)I", (void *)Q_reset },
    { "clearBindings", "(J)V", (void *)Q_clearBindings },
    { "paramCount", "(J)I", (void *)Q_paramCount },
    { "readOnly", "(J)I", (void *)Q_readOnly },
    { "bindNull", "(JI)I", (void *)Q_bindNull },
    { "bindLong", "(JIJ)I", (void *)Q_bindLong },
    { "bindDouble", "(JID)I", (void *)Q_bindDouble },
    { "bindString", "(JILjava/lang/String;)I", (void *)Q_bindString },
    { "bindBlob", "(JI[B)I", (void *)Q_bindBlob },
    { "step", "(J)I", (void *)Q_step },
    { "columnCount", "(J)I", (void *)Q_columnCount },
    { "columnName", "(JI)Ljava/lang/String;", (void *)Q_columnName },
    { "columnType", "(JI)I", (void *)Q_columnType },
    { "columnLong", "(JI)J", (void *)Q_columnLong },
    { "columnDouble", "(JI)D", (void *)Q_columnDouble },
    { "columnText", "(JI)Ljava/lang/String;", (void *)Q_columnText },
    { "columnBlob", "(JI)[B", (void *)Q_columnBlob },
    { "version", "()Ljava/lang/String;", (void *)Q_version },
};

bool husk_register_sqlite(JNIEnv *env)
{
    return shim_register(env, "husk/Sqlite", k_sqlite, (int)(sizeof k_sqlite / sizeof *k_sqlite));
}
