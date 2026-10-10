#!/usr/bin/env bash
# Packs an Android program and ART into an iOS app (.ipa), for signing with a
# sideloading tool (Feather and the like): every Mach-O file in it is a dylib or
# the executable, which such tools sign with the app; the executable carries the
# entitlements (port/ios/entitlements.plist) ad hoc signed here, for them to keep.
#
#   port/ios/pack-ipa.sh --runtime DIR --boot DIR --app DIR --main CLASS --id BUNDLE_ID \
#                        --name NAME --art-src DIR --out APP.ipa
#
#   --runtime  ART's runtime built for iOS: DIR/lib/*.dylib and libsigchain.a (gen-ninja --target ios)
#   --boot     port/art/boot-image.sh --ios output: jars/, arm64/, boot.classpath
#   --app      classes.dex, and oat/arm64/classes.odex{,.vdex,.dylib} compiled with
#              --dex-location=classes.dex (ART resolves it against where the app is)
#   --art-src  ART's sources, for jni.h
# Run on macOS with Xcode.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
while [ $# -gt 0 ]; do
    case "$1" in
        --runtime) RT="$2";; --boot) BOOT="$2";; --app) APPDIR="$2";; --main) MAIN="$2";;
        --id) ID="$2";; --name) NAME="$2";; --art-src) SRC="$2";; --out) OUT="$2";;
        *) echo "unknown option $1" >&2; exit 2;;
    esac
    shift 2
done
SDK=$(xcrun --sdk iphoneos --show-sdk-path)
WORK=$(mktemp -d); APP="$WORK/Payload/$NAME.app"
mkdir -p "$APP/Frameworks" "$APP/art" "$APP/app"

xcrun --sdk iphoneos clang -target arm64-apple-ios14.0 -isysroot "$SDK" -fobjc-arc -O2 \
    -I"$SRC/libnativehelper/include_jni" "$ROOT/port/ios/shell/main.m" -o "$APP/$NAME" \
    -framework UIKit -framework Foundation -L"$RT/lib" -lart -Wl,-force_load,"$RT/lib/libsigchain.a" \
    -Wl,-rpath,@executable_path/Frameworks

cp "$RT"/lib/*.dylib "$APP/Frameworks/"
cp -R "$BOOT/jars" "$BOOT/arm64" "$BOOT/boot.classpath" "$APP/art/"
cp -R "$APPDIR"/classes.dex "$APPDIR"/oat "$APP/app/"

sed -e "s|@NAME@|$NAME|g" -e "s|@ID@|$ID|g" -e "s|@MAIN@|$MAIN|g" "$ROOT/port/ios/Info.plist" > "$APP/Info.plist"
plutil -lint "$APP/Info.plist" > /dev/null

# ad hoc signatures: the signing tool replaces them; the executable's carries the entitlements
for f in "$APP"/Frameworks/*.dylib "$APP"/art/arm64/*.dylib "$APP"/app/oat/arm64/*.dylib; do
    codesign -f -s - "$f"
done
codesign -f -s - --entitlements "$ROOT/port/ios/entitlements.plist" "$APP/$NAME"

OUT_ABS="$(cd "$(dirname "$OUT")" && pwd)/$(basename "$OUT")"
rm -f "$OUT_ABS"
(cd "$WORK" && zip -qry "$OUT_ABS" Payload)
du -sh "$OUT_ABS"
rm -rf "$WORK"
