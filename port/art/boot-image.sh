#!/usr/bin/env bash
# Compiles the boot class path to an arm64 boot image for Apple systems, with the
# installed dex2oat (port/art/build.sh, ART_INSTALL=1), and converts each oat file's
# code to a Mach-O dylib beside it (tools/oat2macho), which is signed there: on
# Apple systems ART maps a boot oat file's code from <oat file>.dylib.
#
#   port/art/boot-image.sh OUT_DIR [--ios|--macos] [EXTRA.jar]...
#
# EXTRA jars (the framework apps run on, port/framework) go on the boot class path after the
# class library's.
#
# OUT_DIR gets jars/ (the boot class path, as compiled: ART checks the jars it runs
# with against the image) and arm64/boot*.{art,oat,vdex,oat.dylib}. Run the image with
#   -Ximage:OUT_DIR/boot.art -Xbootclasspath:<OUT_DIR/jars/*.jar, in boot.classpath's
#   order> -Xbootclasspath-locations:<the same names under /boot>
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
OUT="$1"; TARGET="${2:---ios}"
shift; [ $# -gt 0 ] && shift
EXTRA=("$@")
PREFIX="${PREFIX:-/usr/local}"
JARS="$PREFIX/lib/$(gcc -dumpmachine)/java/dex/art"
# the runtime's default boot class path, in its order
BCP=(core-oj core-libart okhttp bouncycastle apache-xml apachehttp core-junit hamcrest junit-runner wolfssljni)
mkdir -p "$OUT/jars" "$OUT/arm64"
args=(); locs=()
for j in "${BCP[@]}"; do
    cp "$JARS/$j-hostdex.jar" "$OUT/jars/"
    args+=(--dex-file="$OUT/jars/$j-hostdex.jar" --dex-location="/boot/$j-hostdex.jar")
    locs+=("/boot/$j-hostdex.jar")
done
for j in ${EXTRA[@]+"${EXTRA[@]}"}; do
    b=$(basename "$j")
    cp "$j" "$OUT/jars/$b"
    args+=(--dex-file="$OUT/jars/$b" --dex-location="/boot/$b")
    locs+=("/boot/$b")
done
(IFS=:; echo "${locs[*]}") > "$OUT/boot.classpath"
rm -f "$OUT"/arm64/boot*
export ANDROID_DATA="$OUT/android-data"; mkdir -p "$ANDROID_DATA"
dex2oat --runtime-arg -Xms64m --runtime-arg -Xmx512m --instruction-set=arm64 --base=0x60000000 \
    --image="$OUT/arm64/boot.art" --oat-file="$OUT/arm64/boot.oat" "${args[@]}"
cc -std=c11 -O1 "$ROOT/tools/oat2macho.c" -o "$OUT/oat2macho"
for f in "$OUT"/arm64/boot*.oat; do "$OUT/oat2macho" "$TARGET" "$f" "$f.dylib"; done
ls -la "$OUT/arm64"
