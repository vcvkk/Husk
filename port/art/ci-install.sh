#!/usr/bin/env bash
# ART for CI jobs on Linux: built and installed into build/prefix (port/art/build.sh), or taken
# as it is when a cache restored it there; then PATH, LD_LIBRARY_PATH and PREFIX for the steps
# after (GITHUB_ENV). The caches key it by what goes into it: ART's patches, build.sh, dx-via-d8.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
PREFIX="$ROOT/build/prefix"
TRIPLE="$(gcc -dumpmachine)"
if [ -x "$PREFIX/bin/dex2oat" ]; then
    echo "ART from the cache: $PREFIX"
else
    export PATH=/usr/lib/ccache:$PATH CCACHE_DIR=~/.cache/ccache
    export CCACHE_SLOPPINESS=include_file_mtime,include_file_ctime,time_macros CCACHE_MAXSIZE=6G
    ART_INSTALL=1 PREFIX="$PREFIX" "$ROOT/port/art/build.sh"
    sudo chown -R "$(id -u):$(id -g)" "$PREFIX"
    ccache -s | head -8 || true
fi
LIBS="$PREFIX/lib:$PREFIX/lib/$TRIPLE:$PREFIX/lib/$TRIPLE/art"
export PATH="$PREFIX/bin:$PATH" LD_LIBRARY_PATH="$LIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
dex2oat --help > /dev/null 2>&1 || true
command -v dex2oat
if [ -n "${GITHUB_ENV:-}" ]; then
    { echo "PREFIX=$PREFIX"; echo "LD_LIBRARY_PATH=$LD_LIBRARY_PATH"; } >> "$GITHUB_ENV"
    echo "$PREFIX/bin" >> "$GITHUB_PATH"
fi
