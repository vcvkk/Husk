#!/usr/bin/env bash
# What darwin-art.yml's boot job makes, on an arm64 Linux runner with ART installed
# (port/art/build.sh, ART_INSTALL=1): the boot image, and tests/dvm's Hello compiled
# against it, packed for the macOS job (build/boot.tgz) and for the iOS app
# (build/boot-ios.tgz). The job caches both by this script's inputs.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"

port/art/boot-image.sh build/boot --macos

v=$(sed -n 's/^R8_VERSION=//p' port/art/build.sh)
[ -f build/r8.jar ] || curl -fsSL -o build/r8.jar "https://dl.google.com/android/maven2/com/android/tools/r8/$v/r8-$v.jar"
H=build/boot/hello; mkdir -p $H/cls $H/oat/arm64
javac --release 8 -d $H/cls tests/dvm/Hello.java
java -cp build/r8.jar com.android.tools.r8.D8 --min-api 26 --output $H $H/cls/*.class

B="$PWD/build/boot"
bcp=$(tr ':' '\n' < "$B/boot.classpath" | sed "s|^/boot/|$B/jars/|" | paste -sd: -)
# compiles Hello against the image: compile DEX_LOCATION ODEX
compile() {
    ANDROID_DATA="$B/android-data" dex2oat --runtime-arg -Xbootclasspath:"$bcp" \
        --runtime-arg -Xbootclasspath-locations:"$(cat "$B/boot.classpath")" \
        --boot-image="$B/boot.art" --instruction-set=arm64 --class-loader-context='PCL[]' \
        --dex-file=$H/classes.dex --dex-location="$1" --oat-file="$2"
}

# macOS: an odex beside classes.dex is found by the dex file's location, where the
# macOS job runs it
compile /Users/runner/work/Husk/Husk/build/boot/hello/classes.dex $H/oat/arm64/classes.odex
build/boot/oat2macho --macos $H/oat/arm64/classes.odex $H/oat/arm64/classes.odex.dylib
tar -czf build/boot.tgz -C build boot/jars boot/arm64 boot/boot.classpath boot/hello

# iOS: the code as iOS dylibs; Hello with a relative dex location, which ART resolves
# against where the app is installed
rm -rf build/boot-ios; mkdir -p build/boot-ios/hello/oat/arm64
cp -R build/boot/jars build/boot/arm64 build/boot/boot.classpath build/boot-ios/
for f in build/boot-ios/arm64/*.oat; do build/boot/oat2macho --ios "$f" "$f.dylib"; done
cp $H/classes.dex build/boot-ios/hello/
compile classes.dex build/boot-ios/hello/oat/arm64/classes.odex
build/boot/oat2macho --ios build/boot-ios/hello/oat/arm64/classes.odex build/boot-ios/hello/oat/arm64/classes.odex.dylib
tar -czf build/boot-ios.tgz -C build boot-ios
ls -la build/boot.tgz build/boot-ios.tgz
