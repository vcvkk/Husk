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
R8_VERSION=8.13.25
PREFIX="${PREFIX:-/usr/local}"
mkdir -p "$WORK" && cd "$WORK"
export JAVA_TOOL_OPTIONS="${JAVA_TOOL_OPTIONS:-}"
export PKG_CONFIG_PATH="$PREFIX/lib/pkgconfig:$PREFIX/lib/$(gcc -dumpmachine)/pkgconfig:${PKG_CONFIG_PATH:-}"

fetch_rev() {  # url dir rev
    [ -d "$2/.git" ] || { git init -q "$2"; git -C "$2" remote add origin "$1"; }
    git -C "$2" fetch -q --depth 1 origin "$3"
    git -C "$2" checkout -q --force FETCH_HEAD
}

if ! pkg-config --atleast-version=1.8.0 libunwind 2>/dev/null; then
    echo "==> libunwind $LIBUNWIND"
    curl -fsSL "https://github.com/libunwind/libunwind/releases/download/v$LIBUNWIND/libunwind-$LIBUNWIND.tar.gz" | tar xz
    (cd "libunwind-$LIBUNWIND" && ./configure -q --prefix="$PREFIX" --disable-tests --disable-documentation \
        && make -s -j"$(nproc)" && sudo make -s install) >/dev/null
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
make -j"$(nproc)" DX="$HERE/dx-via-d8" > "$WORK/art-build.log" 2>&1 || { grep -m20 -e 'error:' -e 'Error [0-9]' "$WORK/art-build.log"; tail -40 "$WORK/art-build.log"; exit 1; }
ls -la out/host/linux-x86/bin out/host/linux-x86/framework

if [ "${INSTALL:-0}" = 1 ]; then
    echo "==> installing into $PREFIX"
    # make install also copies dx, which d8 replaces
    cp "$HERE/dx-via-d8" out/host/linux-x86/bin/dx
    cp "$R8_JAR" out/host/linux-x86/framework/dx.jar
    sudo make install ____PREFIX="$PREFIX" >/dev/null
    sudo ldconfig
fi
