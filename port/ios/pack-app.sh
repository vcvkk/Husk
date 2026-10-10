#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Packs an Android app, compiled ahead of time on a boot image with the framework, into an iOS app
# (.ipa) for a sideloading tool to sign (Feather and the like): port/ios/shell/app.m, ART's runtime,
# the framework's natives, the boot image and the app, every Mach-O file the executable or a dylib.
#
#   port/ios/pack-app.sh --runtime DIR --files DIR --natives LIB.a --name NAME --id BUNDLE_ID --out APP.ipa
#
#   --runtime  ART's runtime for iOS: DIR/lib/*.dylib and libsigchain.a, DIR/include/nativehelper
#   --files    port/framework/ci-app.sh's iOS output: boot/ (jars/, arm64/, boot.classpath), app/
#   --natives  the framework's natives for iOS (port/framework/build-natives.sh ios)
# Run on macOS with Xcode.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
while [ $# -gt 0 ]; do
    case "$1" in
        --runtime) RT="$2";; --files) FILES="$2";; --natives) NATIVES="$2";; --name) NAME="$2";;
        --id) ID="$2";; --out) OUT="$2";;
        *) echo "unknown option $1" >&2; exit 2;;
    esac
    shift 2
done
SDK=$(xcrun --sdk iphoneos --show-sdk-path)
WORK=$(mktemp -d); APP="$WORK/Payload/$NAME.app"
mkdir -p "$APP/Frameworks" "$APP/art"

xcrun --sdk iphoneos clang -target arm64-apple-ios14.0 -isysroot "$SDK" -fobjc-arc -O2 \
    -I"$RT/include/nativehelper" -I"$ROOT/port/framework/jni" "$ROOT/port/ios/shell/app.m" "$NATIVES" -o "$APP/$NAME" \
    -framework UIKit -framework Foundation -framework QuartzCore -framework CoreGraphics -framework CoreText \
    -framework ImageIO -L"$RT/lib" -lart -Wl,-force_load,"$RT/lib/libsigchain.a" -lc++ -lz \
    -Wl,-rpath,@executable_path/Frameworks

cp "$RT"/lib/*.dylib "$APP/Frameworks/"
cp -R "$FILES/boot/jars" "$FILES/boot/arm64" "$FILES/boot/boot.classpath" "$APP/art/"
cp -R "$FILES/app" "$APP/app"

sed -e "s|@NAME@|$NAME|g" -e "s|@ID@|$ID|g" -e "s|@MAIN@||g" "$ROOT/port/ios/Info.plist" > "$APP/Info.plist"
plutil -lint "$APP/Info.plist" > /dev/null

for f in "$APP"/Frameworks/*.dylib "$APP"/art/arm64/*.dylib "$APP"/app/oat/arm64/*.dylib; do
    codesign -f -s - "$f"
done
codesign -f -s - --entitlements "$ROOT/port/ios/entitlements.plist" "$APP/$NAME"

OUT_ABS="$(cd "$(dirname "$OUT")" && pwd)/$(basename "$OUT")"
rm -f "$OUT_ABS"
(cd "$WORK" && zip -qry "$OUT_ABS" Payload)
du -sh "$OUT_ABS"
rm -rf "$WORK"
