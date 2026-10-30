#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# ANGLE (EGL and OpenGL ES over Metal) for iOS arm64 or macOS arm64, from WebKit's tree (it carries an Xcode project: no
# depot_tools, no gn), as Husk's main line builds it (scripts/build_angle_ios.sh):
#
#   port/gl/build-angle.sh ios|macos OUT_DIR       ->  OUT_DIR/lib/libANGLE-shared.dylib, OUT_DIR/include
#
# WebKit renames ANGLE's EGL entry points (EGL_ChooseConfig); a second link adds the standard names as aliases (ld's
# -alias_list). The GL ones stay as they are: they are reached through eglGetProcAddress.
set -euo pipefail
TARGET="$1"; OUT="$(mkdir -p "$2" && cd "$2" && pwd)"
case "$TARGET" in
    ios) SDK=iphoneos; DEPLOY=(IPHONEOS_DEPLOYMENT_TARGET=14.0) ;;
    macos) SDK=macosx; DEPLOY=(MACOSX_DEPLOYMENT_TARGET=11.0) ;;
    *) echo "ios or macos" >&2; exit 2 ;;
esac
WORK="${ANGLE_WORK:-$PWD/build/angle-src}"
WK="$WORK/webkit"
mkdir -p "$WORK"
if [ ! -d "$WK" ]; then
    git clone -q --depth 1 --filter=blob:none --sparse https://github.com/WebKit/WebKit.git "$WK"
    (cd "$WK" && git sparse-checkout set Source/ThirdParty/ANGLE Configurations Tools/Scripts Tools/ccache)
fi
cd "$WK/Source/ThirdParty/ANGLE"
LOG="$OUT/angle-build.log"
# WebKit's tree is built with the newest Xcode: its thread-safety annotations on std::mutex need a libc++ that has them, and its
# warnings are errors. Neither changes what is built.
NOWARN="-Wno-unguarded-availability-new -Wno-thread-safety-attributes -Wno-thread-safety-analysis -Wno-error"
build () {
    local ld=()
    [ -n "${1:-}" ] && ld=("OTHER_LDFLAGS=\$(inherited) -Wl,-alias_list,$1")
    rm -rf ANGLE.xcarchive
    env -i PATH="$PATH" HOME="$HOME" xcodebuild archive -archivePath ANGLE -scheme ANGLE -sdk $SDK -arch arm64 -configuration Release \
        WEBCORE_LIBRARY_DIR=/usr/local/lib NORMAL_UMBRELLA_FRAMEWORKS_DIR= CODE_SIGNING_ALLOWED=NO CODE_SIGNING_REQUIRED=NO \
        WK_AVAILABILITY_OVERLAY_FLAGS= WK_AVAILABILITY_OVERLAY_SWIFT_FLAGS= "${DEPLOY[@]}" \
        GCC_TREAT_WARNINGS_AS_ERRORS=NO \
        OTHER_CFLAGS="\$(inherited) $NOWARN" OTHER_CPLUSPLUSFLAGS="\$(inherited) $NOWARN" \
        ${ld[@]+"${ld[@]}"} > "$LOG" 2>&1 || { echo "ANGLE build failed:" >&2; grep -a "error:\|^ld: " "$LOG" | head -20 >&2; exit 1; }
}
build ""
DYLIB=ANGLE.xcarchive/Products/usr/local/lib/libANGLE-shared.dylib
nm -g "$DYLIB" | awk '$2=="T"{print $3}' | grep -E '^_EGL_' | awk '{o=$1; a=$1; sub(/^_EGL_/,"_egl",a); print o, a}' > "$OUT/aliases.txt"
build "$OUT/aliases.txt"
mkdir -p "$OUT/lib" "$OUT/include"
cp "$DYLIB" "$OUT/lib/"
rsync -a include/ "$OUT/include/"
install_name_tool -id @rpath/libANGLE-shared.dylib "$OUT/lib/libANGLE-shared.dylib"
ls -la "$OUT/lib/libANGLE-shared.dylib"
echo "EGL entry points: $(nm -g "$OUT/lib/libANGLE-shared.dylib" | grep -c ' T _egl')"
