#!/usr/bin/env bash
# Builds the libraries ART's runtime links, for iOS arm64, as static libraries:
# lz4, liblzma (xz), expat, epoll-shim and ICU (libicucore has no C++ API, which
# libjavacore uses; its data is linked in). zlib comes with the SDK; of OpenSSL
# only opensslv.h is read (System.c), and compat-ios has one.
#
#   port/art/darwin/ios-deps.sh PREFIX     (on macOS with Xcode)
#
# PREFIX gets include/ and lib/.
set -euo pipefail
PREFIX="$(mkdir -p "$1" && cd "$1" && pwd)"
MIN=14.0
SDK=$(xcrun --sdk iphoneos --show-sdk-path)
CC="$(xcrun --sdk iphoneos -f clang)"
CXX="$(xcrun --sdk iphoneos -f clang++)"
CFLAGS="-target arm64-apple-ios$MIN -isysroot $SDK -O2"
WORK="$PREFIX/src"; mkdir -p "$WORK" "$PREFIX/include" "$PREFIX/lib"
cmake_ios=(-DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET=$MIN
           -DCMAKE_OSX_SYSROOT="$SDK" -DBUILD_SHARED_LIBS=OFF -DCMAKE_BUILD_TYPE=Release
           -DCMAKE_INSTALL_PREFIX="$PREFIX" -DBUILD_TESTING=OFF)
fetch() { [ -d "$WORK/$2" ] || { curl -fsSL "$1" | tar -xz -C "$WORK"; }; }

# lz4: the library is four C files
fetch https://github.com/lz4/lz4/archive/refs/tags/v1.10.0.tar.gz lz4-1.10.0
(cd "$WORK/lz4-1.10.0/lib" && for f in lz4 lz4hc lz4frame xxhash; do $CC $CFLAGS -c $f.c -o $f.o; done \
  && ar rcs "$PREFIX/lib/liblz4.a" lz4.o lz4hc.o lz4frame.o xxhash.o && cp lz4.h lz4hc.h lz4frame.h "$PREFIX/include/")

fetch https://github.com/tukaani-project/xz/releases/download/v5.6.3/xz-5.6.3.tar.gz xz-5.6.3
cmake -S "$WORK/xz-5.6.3" -B "$WORK/xz-build" "${cmake_ios[@]}" -DXZ_TOOL_XZ=OFF -DXZ_TOOL_XZDEC=OFF \
  -DXZ_TOOL_LZMADEC=OFF -DXZ_TOOL_LZMAINFO=OFF -DXZ_TOOL_SCRIPTS=OFF -DXZ_NLS=OFF -DXZ_DOC=OFF > /dev/null
cmake --build "$WORK/xz-build" -j8 --target install > /dev/null

fetch https://github.com/libexpat/libexpat/releases/download/R_2_6_4/expat-2.6.4.tar.gz expat-2.6.4
cmake -S "$WORK/expat-2.6.4" -B "$WORK/expat-build" "${cmake_ios[@]}" -DEXPAT_BUILD_TOOLS=OFF \
  -DEXPAT_BUILD_EXAMPLES=OFF -DEXPAT_BUILD_TESTS=OFF -DEXPAT_BUILD_DOCS=OFF > /dev/null
cmake --build "$WORK/expat-build" -j8 --target install > /dev/null

fetch https://github.com/jiixyj/epoll-shim/archive/refs/tags/v0.0.20240608.tar.gz epoll-shim-0.0.20240608
cmake -S "$WORK/epoll-shim-0.0.20240608" -B "$WORK/epoll-build" "${cmake_ios[@]}" > /dev/null
cmake --build "$WORK/epoll-build" -j8 --target install > /dev/null

# ICU: built for the Mac first, whose tools the iOS build runs; data linked statically
fetch https://github.com/unicode-org/icu/releases/download/release-74-2/icu4c-74_2-src.tgz icu
if [ ! -f "$PREFIX/lib/libicuuc.a" ]; then
  mkdir -p "$WORK/icu-host" "$WORK/icu-ios"
  (cd "$WORK/icu-host" && "$WORK/icu/source/configure" --disable-tests --disable-samples > /dev/null && make -j8 > /dev/null)
  (cd "$WORK/icu-ios" && CC="$CC" CXX="$CXX" CFLAGS="$CFLAGS" CXXFLAGS="$CFLAGS -std=c++17" LDFLAGS="$CFLAGS" \
     "$WORK/icu/source/configure" --host=aarch64-apple-darwin --with-cross-build="$WORK/icu-host" \
     --enable-static --disable-shared --with-data-packaging=static --disable-tools --disable-tests \
     --disable-samples --disable-extras --prefix="$PREFIX" > /dev/null \
   && make -j8 > /dev/null && make install > /dev/null)
fi
ls -la "$PREFIX/lib"
