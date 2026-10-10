#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
# An APK compiled ahead of time on a boot image that has the framework (port/framework) in it, for
# the macOS test host and for an iOS app, on an arm64 Linux runner with ART installed:
#
#   port/framework/ci-app.sh APP.apk OUT_DIR [FRAMEWORK_RES.apk]
#
#   OUT_DIR/macos.tgz, OUT_DIR/ios.tgz: boot/ (jars/, arm64/, boot.classpath) and app/ (base.apk,
#   oat/arm64/base.{odex,vdex,odex.dylib}, and framework-res.apk, the platform's resources, when given),
#   the code as Mach-O for that system. The app is compiled
#   with the dex location base.apk, which ART resolves against where the APK is.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
APK="$(realpath "$1")"; OUT="$(realpath -m "$2")"; FWRES="${3:+$(realpath "$3")}"
mkdir -p "$OUT"
v=$(sed -n 's/^R8_VERSION=//p' "$ROOT/port/art/build.sh")
export R8_JAR="${R8_JAR:-$OUT/r8.jar}"
[ -f "$R8_JAR" ] || curl -fsSL -o "$R8_JAR" "https://dl.google.com/android/maven2/com/android/tools/r8/$v/r8-$v.jar"

"$ROOT/port/framework/build.sh" "$OUT/framework.jar"
"$ROOT/port/art/boot-image.sh" "$OUT/macos/boot" --macos "$OUT/framework.jar"

B="$OUT/macos/boot"
bcp=$(tr ':' '\n' < "$B/boot.classpath" | sed "s|^/boot/|$B/jars/|" | paste -sd: -)
A="$OUT/macos/app"; mkdir -p "$A/oat/arm64"
cp "$APK" "$A/base.apk"
[ -n "$FWRES" ] && cp "$FWRES" "$A/framework-res.apk"
SECONDS=0
ANDROID_DATA="$B/android-data" dex2oat --runtime-arg -Xbootclasspath:"$bcp" \
    --runtime-arg -Xbootclasspath-locations:"$(cat "$B/boot.classpath")" --runtime-arg -Xmx1g \
    --boot-image="$B/boot.art" --instruction-set=arm64 --class-loader-context='PCL[]' \
    --dex-file="$A/base.apk" --dex-location=base.apk --oat-file="$A/oat/arm64/base.odex"
echo "the app compiled in $SECONDS s"
"$B/oat2macho" --macos "$A/oat/arm64/base.odex" "$A/oat/arm64/base.odex.dylib"
tar -czf "$OUT/macos.tgz" -C "$OUT/macos" boot/jars boot/arm64 boot/boot.classpath app

# iOS: the same files, the code as iOS dylibs
rm -rf "$OUT/ios"; mkdir -p "$OUT/ios"
cp -R "$OUT/macos/boot" "$OUT/macos/app" "$OUT/ios/"
rm -rf "$OUT/ios/boot/android-data"
for f in "$OUT"/ios/boot/arm64/*.oat "$OUT/ios/app/oat/arm64/base.odex"; do "$B/oat2macho" --ios "$f" "$f.dylib"; done
tar -czf "$OUT/ios.tgz" -C "$OUT/ios" boot/jars boot/arm64 boot/boot.classpath app
ls -la "$OUT"/*.tgz
