#!/usr/bin/env bash
# Builds ATL's art_standalone (ART from Android 10, with dex2oat and libcore) on
# the machine it runs on, with husk-port's patches. Supported hosts are the ones
# art_standalone supports: Linux on x86_64 or arm64. An arm64 host gives a
# dex2oat that compiles for arm64, which is what the iPhone needs.
#
#   port/art/build.sh [WORKDIR]      (needs: the packages in .github/workflows/art.yml)
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
WORK="$(realpath -m "${1:-$HERE/../../build/art}")"
ART_REV=66a5d9079b159e9c5819bfeef4a6fff6a5f32719
BIONIC_REV=6253dec7f24a57a94039a088cb436d880e92e3be
LIBUNWIND=1.8.1
VIXL=8.0.0            # ART's arm64 back end; its API changed after this
WOLFSSL=v5.8.2-stable # with JNI, which distributions leave out
R8_VERSION=8.13.25
PREFIX="${PREFIX:-/usr/local}"
mkdir -p "$WORK" && cd "$WORK"
export JAVA_TOOL_OPTIONS="${JAVA_TOOL_OPTIONS:-}"
export PKG_CONFIG_PATH="$PREFIX/lib/pkgconfig:$PREFIX/lib/$(gcc -dumpmachine)/pkgconfig:${PKG_CONFIG_PATH:-}"
# what is built here (libunwind 1.8, which bionic_translation needs) before the system's: the
# linker resolves libdl_bio's libunwind.so.8 through these too
export LIBRARY_PATH="$PREFIX/lib:$PREFIX/lib/$(gcc -dumpmachine)${LIBRARY_PATH:+:$LIBRARY_PATH}"
export LD_LIBRARY_PATH="$PREFIX/lib:$PREFIX/lib/$(gcc -dumpmachine)${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"

fetch_rev() {  # url dir rev
    [ -d "$2/.git" ] || { git init -q "$2"; git -C "$2" remote add origin "$1"; }
    git -C "$2" fetch -q --depth 1 origin "$3"
    git -C "$2" checkout -q --force FETCH_HEAD
}

if ! pkg-config --atleast-version=1.8.0 libunwind 2>/dev/null; then
    echo "==> libunwind $LIBUNWIND"
    curl -fsSL "https://github.com/libunwind/libunwind/releases/download/v$LIBUNWIND/libunwind-$LIBUNWIND.tar.gz" | tar xz
    # in the multiarch directory, which ld.so searches before the system's (where Ubuntu's 1.6
    # can be, as a dependency of other packages)
    (cd "libunwind-$LIBUNWIND" && ./configure -q --prefix="$PREFIX" --libdir="$PREFIX/lib/$(gcc -dumpmachine)" \
        --disable-tests --disable-documentation \
        && make -s -j"$(nproc)" && sudo make -s install) >/dev/null
    sudo ldconfig
fi

if ! [ -f "$PREFIX/include/vixl/aarch64/disasm-aarch64.h" ]; then
    echo "==> VIXL $VIXL"
    rm -rf vixl && git clone -q --depth 1 -b "$VIXL" https://github.com/Linaro/vixl.git vixl
    # Alpine's meson build for VIXL (aports testing/vixl, BSD-3-Clause)
    (cd vixl && patch -p1 -s < "$HERE/vixl-meson.patch" \
        && meson setup build --prefix="$PREFIX" -Dtarget=a64,t32 -Dsimulator=none --buildtype=release >/dev/null \
        && ninja -C build >/dev/null && sudo ninja -C build install >/dev/null)
    sudo ldconfig
fi

if ! nm -D "$PREFIX/lib/libwolfssl.so" 2>/dev/null | grep -q wolfSSL_set_jobject; then
    echo "==> wolfSSL $WOLFSSL with JNI"
    rm -rf wolfssl && git clone -q --depth 1 -b "$WOLFSSL" https://github.com/wolfSSL/wolfssl.git wolfssl
    (cd wolfssl && autoreconf -i >/dev/null 2>&1 \
        && ./configure -q --prefix="$PREFIX" --enable-shared --disable-opensslall --disable-opensslextra \
           --enable-aescbc-length-checks --enable-curve25519 --enable-ed25519 --enable-ed25519-stream --enable-oldtls \
           --enable-base64encode --enable-tlsx --enable-scrypt --disable-examples --enable-crl --enable-jni \
           --enable-sessioncerts >/dev/null \
        && make -s -j"$(nproc)" >/dev/null && sudo make -s install >/dev/null)
    sudo ldconfig
fi

echo "==> bionic_translation"
fetch_rev https://gitlab.com/android_translation_layer/bionic_translation.git bionic_translation "$BIONIC_REV"
(cd bionic_translation && meson setup build --prefix="$PREFIX" -Dbuildtype=release --reconfigure >/dev/null \
    && ninja -C build >/dev/null && sudo ninja -C build install >/dev/null)
sudo ldconfig

echo "==> d8 $R8_VERSION"
export R8_JAR="$WORK/r8-$R8_VERSION.jar"
[ -f "$R8_JAR" ] || curl -fsSL -o "$R8_JAR" "https://dl.google.com/android/maven2/com/android/tools/r8/$R8_VERSION/r8-$R8_VERSION.jar"

echo "==> art_standalone $ART_REV"
fetch_rev https://gitlab.com/android_translation_layer/art_standalone.git art_standalone "$ART_REV"
for p in "$HERE"/patches/*.patch; do git -C art_standalone apply "$p"; done
cd art_standalone
# an x86_64 host compiles for arm64 too (patches/0004; the arm64 back end needs VIXL)
case "$(uname -m)" in x86_64) ARCHS="arm arm64 x86 x86_64";; *) ARCHS="";; esac
make -j"$(nproc)" ${ARCHS:+ART_HOST_CODEGEN_ARCHS="$ARCHS"} DX="$HERE/dx-via-d8" > "$WORK/art-build.log" 2>&1 || { grep -m20 -e 'error:' -e 'Error [0-9]' "$WORK/art-build.log"; tail -40 "$WORK/art-build.log"; exit 1; }
ls -la out/host/linux-x86/bin out/host/linux-x86/framework

if [ "${ART_INSTALL:-0}" = 1 ]; then
    echo "==> installing into $PREFIX"
    # make install also copies dx, which d8 replaces
    cp "$HERE/dx-via-d8" out/host/linux-x86/bin/dx
    cp "$R8_JAR" out/host/linux-x86/framework/dx.jar
    sudo make install ____PREFIX="$PREFIX" >/dev/null
    sudo ldconfig
fi
