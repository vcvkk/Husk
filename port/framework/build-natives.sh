#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Builds the framework's natives (jni/) as libhuskframework.a for macOS or iOS, and on macOS the
# test host (host/mac-host.c):
#
#   port/framework/build-natives.sh macos|ios OUT_DIR JNI_INCLUDE_DIR [ART_LIB_DIR]
#
# JNI_INCLUDE_DIR has jni.h (ART's libnativehelper/include/nativehelper); ART_LIB_DIR has
# libart.dylib and libsigchain.a, for the host.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
TARGET="$1"; OUT="$(mkdir -p "$2" && cd "$2" && pwd)"; JNI="$3"; ARTLIB="${4:-}"
case "$TARGET" in
    macos) SDK=macosx; TRIPLE=arm64-apple-macos11.0 ;;
    ios) SDK=iphoneos; TRIPLE=arm64-apple-ios14.0 ;;
    *) echo "macos or ios" >&2; exit 2 ;;
esac
CC=(xcrun --sdk $SDK clang -target $TRIPLE -isysroot "$(xcrun --sdk $SDK --show-sdk-path)" -O2 -g
    -Wall -Wno-incompatible-pointer-types -Wno-unused-function -Wno-unused-variable -I"$JNI")
python3 "$HERE/jni/gen-jni.py" "$HERE/jni/gfx.c" k_gfx husk/Gfx husk_register_gfx "$OUT/gfx_jni.c"
cp "$HERE/jni/gfx.c" "$OUT/gfx.c"
python3 "$HERE/jni/gen-jni.py" "$HERE/jni/web.m" k_web husk/Web husk_register_web "$OUT/web_jni.m"
python3 "$HERE/jni/gen-jni.py" "$HERE/jni/audio.c" k_audio husk/Audio husk_register_audio "$OUT/audio_jni.c"
# android.opengl.GLES10..GLES32's natives (jni/gles/, the main line's generated ones), through ANGLE (jni/gl.c)
gles=()
for g in gles20:GLES20 gles30:GLES30 gles31:GLES31 gles32:GLES32 gles10:GLES10 gles10ext:GLES10Ext gles11:GLES11 gles11ext:GLES11Ext; do
    python3 "$HERE/jni/gen-jni.py" "$HERE/jni/gles/${g%%:*}.c" "k_${g%%:*}" "android/opengl/${g##*:}" "husk_register_${g%%:*}" "$OUT/${g%%:*}_jni.c" > /dev/null
    gles+=("$OUT/${g%%:*}_jni.c")
done
objs=()
for f in "$OUT/gfx_jni.c" "$OUT/web_jni.m" "$OUT/audio_jni.c" "${gles[@]}" "$HERE/jni/gl.c" "$HERE/jni/nativelibs.c" "$HERE/jni/shim.c" "$HERE/jni/native.c" "$HERE/jni/sqlite.c" "$HERE/jni/launch.c"; do
    o="$OUT/$(basename "${f%.*}").o"
    case "$f" in *.m) arc=(-fobjc-arc) ;; *) arc=() ;; esac
    "${CC[@]}" ${arc[@]+"${arc[@]}"} -I"$HERE/jni" -I"$HERE/jni/gles" -c "$f" -o "$o"
    objs+=("$o")
done
# the guest linker and its bionic (port/native/tl, Husk's main line's), its calls to its own Java world's JNI renamed (glue.c)
NCC=(-fno-strict-aliasing -Wno-unused-parameter -I"$HERE/../native/tl" -Dtl_jni_call=tln_jni_call -Dtl_jni_get_field=tln_jni_get_field
     -Dtl_jni_ref=tln_jni_ref -Dtl_jni_pending=tln_jni_pending -Dtl_jni_clear=tln_jni_clear)
# and the JIT region StikDebug grants at launch (ios/: Husk's main line's husk-ios-jit and its brk requests)
for f in "$HERE"/../native/tl/*.c "$HERE"/../native/glue.c "$HERE"/../native/ios/husk-ios-jit.c "$HERE"/../native/ios/husk-brk.S; do
    o="$OUT/native-$(basename "${f%.*}").o"
    "${CC[@]}" "${NCC[@]}" -I"$HERE/../native/ios" -c "$f" -o "$o"
    objs+=("$o")
done
rm -f "$OUT/libhuskframework.a"
ar rcs "$OUT/libhuskframework.a" "${objs[@]}"
echo "$OUT/libhuskframework.a"
if [ "$TARGET" = macos ] && [ -n "$ARTLIB" ]; then
    "${CC[@]}" "$HERE/host/mac-host.c" "$OUT/libhuskframework.a" -o "$OUT/mac-host" \
        -L"$ARTLIB" -lart -Wl,-force_load,"$ARTLIB/libsigchain.a" -lc++ -lz -lsqlite3 \
        -framework CoreFoundation -framework CoreGraphics -framework CoreText -framework ImageIO \
        -framework Foundation -framework AppKit -framework WebKit -framework AudioToolbox -framework Security -Wl,-rpath,"$ARTLIB"
    echo "$OUT/mac-host"
fi
