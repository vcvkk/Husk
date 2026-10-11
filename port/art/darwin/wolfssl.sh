#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
# libwolfssljni.dylib for macOS or iOS: the native half of wolfssljni (ART's external/wolfssljni,
# the JSSE provider security.properties names first: TLS, and the TrustManagerFactory and
# KeyManagerFactory apps' HTTP clients ask for), with wolfSSL linked in statically.
#
#   port/art/darwin/wolfssl.sh macos|ios OUT_LIB_DIR JNI_INCLUDE_DIR WOLFSSLJNI_DIR WORK_DIR
#
# WORK_DIR keeps wolfSSL's build (WORK_DIR/prefix), for a cache. Run on macOS with Xcode.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
TARGET="$1"; OUT="$(mkdir -p "$2" && cd "$2" && pwd)"; JNI="$(cd "$3" && pwd)"; SRC="$(cd "$4" && pwd)"
WORK="$(mkdir -p "$5" && cd "$5" && pwd)"
case "$TARGET" in
    macos) SDK=macosx; TRIPLE=arm64-apple-macos11.0 ;;
    ios) SDK=iphoneos; TRIPLE=arm64-apple-ios14.0 ;;
    *) echo "macos or ios" >&2; exit 2 ;;
esac
SYSROOT=$(xcrun --sdk $SDK --show-sdk-path)
CC="$(xcrun --sdk $SDK -f clang)"
CFLAGS="-target $TRIPLE -isysroot $SYSROOT -O2"
WOLFSSL=$(sed -n 's/^WOLFSSL=\([^ ]*\).*/\1/p' "$ROOT/port/art/build.sh")
P="$WORK/prefix"

if [ ! -f "$P/lib/libwolfssl.a" ]; then
    for t in autoconf automake libtool; do command -v ${t/libtool/glibtoolize} > /dev/null || brew install $t > /dev/null; done
    rm -rf "$WORK/wolfssl"
    git clone -q --depth 1 -b "$WOLFSSL" https://github.com/wolfSSL/wolfssl.git "$WORK/wolfssl"
    # as port/art/build.sh configures it for Linux, static
    (cd "$WORK/wolfssl" && ./autogen.sh > /dev/null 2>&1 \
        && CC="$CC" CFLAGS="$CFLAGS" LDFLAGS="$CFLAGS" ./configure -q --host=aarch64-apple-darwin --prefix="$P" \
           --enable-static --disable-shared --disable-opensslall --disable-opensslextra \
           --enable-aescbc-length-checks --enable-curve25519 --enable-ed25519 --enable-ed25519-stream --enable-oldtls \
           --enable-base64encode --enable-tlsx --enable-scrypt --disable-examples --disable-crypttests --enable-crl \
           --enable-jni --enable-sessioncerts > /dev/null \
        && make -s -j8 > /dev/null && make -s install > /dev/null)
fi

# art_standalone's wolfssljni logs through Android's liblog: to stderr here, as ART's own log goes
cat > "$WORK/android_log.c" <<'EOF2'
#include <stdarg.h>
#include <stdio.h>
int __android_log_print(int prio, const char *tag, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "%s: ", tag);
    int n = vfprintf(stderr, fmt, ap);
    va_end(ap);
    (void)prio;
    return n;
}
EOF2
objs=()
for f in "$SRC"/native/*.c "$WORK/android_log.c"; do
    o="$WORK/$(basename "$f" .c).o"
    "$CC" $CFLAGS -Wno-incompatible-pointer-types -Wno-deprecated-declarations -I"$P/include" -I"$JNI" \
        -I"$SRC/native" -c "$f" -o "$o"
    objs+=("$o")
done
"$CC" $CFLAGS -dynamiclib -install_name @rpath/libwolfssljni.dylib "${objs[@]}" "$P/lib/libwolfssl.a" \
    -framework Security -framework CoreFoundation -o "$OUT/libwolfssljni.dylib"
echo "$OUT/libwolfssljni.dylib"
