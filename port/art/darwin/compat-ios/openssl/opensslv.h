/* SPDX-License-Identifier: GPL-2.0-or-later */
/* What libopenjdk reads of OpenSSL on iOS: the version text for android.openssl.version
 * (System.c). No OpenSSL is linked; Conscrypt brings its own BoringSSL. */
#ifndef OPENSSL_OPENSSLV_H
#define OPENSSL_OPENSSLV_H
#define OPENSSL_VERSION_TEXT "none (husk-port, iOS)"
#endif
