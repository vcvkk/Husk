#!/usr/bin/env bash
# Compiles an APK, and the class library and framework it runs on, to arm64
# ahead of time, and converts every oat file to a Mach-O dylib for iOS:
#
#   port/compile-apk.sh app.apk OUT [framework.jar]
#
#   OUT/arm64/boot*.{art,oat,vdex}   the boot image: libcore, and the framework
#   OUT/app.{oat,vdex}               the app
#   OUT/ios/*.dylib                  every oat file as a dylib (oat2macho --ios)
#
# Needs the dex2oat port/art/build.sh installs; on an x86_64 host it has the
# arm64 back end (patches/0004). The framework is ATL's api-impl.jar; without it
# the app's classes that use android.* fail verification and stay interpreted.
set -euo pipefail
APK="$(realpath "${1:?usage: compile-apk.sh app.apk OUT [framework.jar]}")"
OUT="$(realpath -m "${2:?usage: compile-apk.sh app.apk OUT [framework.jar]}")"
FRAMEWORK="${3:-}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DEX2OAT="${DEX2OAT:-dex2oat}"
ISA=arm64
FILTER="${FILTER:-speed}"

# the class library dalvikvm uses, where art_standalone installs it
LIB="$(dirname "$(readlink -f "$(command -v "$DEX2OAT")")")/../lib"
JARS="$(realpath "$(ls -d "$LIB"/*/java/dex/art 2>/dev/null | head -1)")"
[ -d "$JARS" ] || { echo "no installed class library under $LIB"; exit 1; }
BCP=""
for j in core-oj core-libart okhttp bouncycastle apache-xml wolfssljni; do
    BCP="$BCP:$JARS/$j-hostdex.jar"
done
[ -n "$FRAMEWORK" ] && BCP="$BCP:$(realpath "$FRAMEWORK")"
BCP="${BCP#:}"

mkdir -p "$OUT/$ISA" "$OUT/ios"
export ANDROID_DATA="${ANDROID_DATA:-$OUT/android-data}"
mkdir -p "$ANDROID_DATA"

echo "==> boot image ($ISA)"
args=()
for j in ${BCP//:/ }; do args+=(--dex-file="$j" --dex-location="$j"); done
"$DEX2OAT" "${args[@]}" --image="$OUT/$ISA/boot.art" --oat-file="$OUT/$ISA/boot.oat" \
    --instruction-set=$ISA --base=0x70000000 \
    --runtime-arg -Xbootclasspath:"$BCP" --runtime-arg -Xbootclasspath-locations:"$BCP" \
    > "$OUT/boot-dex2oat.log" 2>&1 || { tail -20 "$OUT/boot-dex2oat.log"; exit 1; }

echo "==> $(basename "$APK") ($ISA, $FILTER)"
# ART finds the image for an ISA at <dir>/<isa>/boot.art
"$DEX2OAT" --dex-file="$APK" --dex-location="$APK" --oat-file="$OUT/app.oat" \
    --instruction-set=$ISA --boot-image="$OUT/boot.art" --compiler-filter="$FILTER" \
    --runtime-arg -Xbootclasspath:"$BCP" --runtime-arg -Xbootclasspath-locations:"$BCP" \
    > "$OUT/app-dex2oat.log" 2>&1 || { tail -20 "$OUT/app-dex2oat.log"; exit 1; }
grep -a 'dex2oat took' "$OUT/app-dex2oat.log" | sed 's/.*dex2oat took/  took/' | cut -d' ' -f1-4

echo "==> Mach-O for iOS"
O2M="$OUT/oat2macho"
cc -std=c11 -O1 "$ROOT/tools/oat2macho.c" -o "$O2M"
"$O2M" --ios --install-name @rpath/app.dylib "$OUT/app.oat" "$OUT/ios/app.dylib"
for f in "$OUT/$ISA"/boot*.oat; do
    b="$(basename "$f" .oat)"
    "$O2M" --ios --install-name "@rpath/$b.dylib" "$f" "$OUT/ios/$b.dylib"
done
du -sh "$OUT/ios"
