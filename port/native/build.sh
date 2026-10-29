#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# The native-library layer (port/native/tl: Husk's main line's guest linker, bionic and JIT-less dylibs) as libhusknative.a for
# macOS or iOS, and what it still needs from outside listed (undefined.txt).
#
#   port/native/build.sh macos|ios OUT_DIR
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
TARGET="$1"; OUT="$(mkdir -p "$2" && cd "$2" && pwd)"
case "$TARGET" in
    macos) SDK=macosx; TRIPLE=arm64-apple-macos11.0 ;;
    ios) SDK=iphoneos; TRIPLE=arm64-apple-ios14.0 ;;
    *) echo "macos or ios" >&2; exit 2 ;;
esac
CC=(xcrun --sdk $SDK clang -target $TRIPLE -isysroot "$(xcrun --sdk $SDK --show-sdk-path)" -O2 -g -fno-strict-aliasing
    -Wall -Wno-unused-function -Wno-unused-variable -Wno-unused-parameter -I"$HERE/tl")
objs=(); fail=0
for f in "$HERE"/tl/*.c "$HERE"/tl/*.m; do
    [ -e "$f" ] || continue
    o="$OUT/$(basename "${f%.*}").o"
    if "${CC[@]}" -c "$f" -o "$o" 2> "$o.log"; then objs+=("$o"); else echo "== $(basename "$f")"; grep -m 40 "error" "$o.log"; fail=1; fi
done
rm -f "$OUT/libhusknative.a"
[ ${#objs[@]} -gt 0 ] && ar rcs "$OUT/libhusknative.a" "${objs[@]}"
# what the objects use and none of them defines
nm -gU "${objs[@]}" 2>/dev/null | awk 'NF>=3 {print $3}' | sort -u > "$OUT/defined.txt"
nm -gu "${objs[@]}" 2>/dev/null | grep -v ':$' | awk '{print $NF}' | sort -u > "$OUT/used.txt"
comm -23 "$OUT/used.txt" "$OUT/defined.txt" > "$OUT/undefined-all.txt"
# the system's own (libSystem, frameworks) are fine: check against a link
"${CC[@]}" -dynamiclib -o "$OUT/probe.dylib" "${objs[@]}" -lz -framework CoreFoundation -framework Security -undefined dynamic_lookup 2>/dev/null || true
nm -u "$OUT/probe.dylib" 2>/dev/null | sort -u > "$OUT/undefined.txt" || true
echo "objects: ${#objs[@]}, compile failures: $fail"
echo "undefined after link (dynamic_lookup):"; cat "$OUT/undefined.txt" | grep -v "^_\(OS\|CF\|Sec\|dispatch\|os_\|_\)" | head -300
exit $fail
