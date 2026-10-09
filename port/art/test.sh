#!/usr/bin/env bash
# Runs tests/dvm's programs on the installed ART (dalvikvm) and compares their
# output with the host JVM's, as tests/dvm/run.sh recorded it. dalvikvm compiles
# each one ahead of time with dex2oat for the machine's own architecture, so on
# an arm64 host this exercises arm64 AOT code. The compiled files are copied to
# $OUT/oat for inspection.
#
#   tests/dvm/run.sh && port/art/test.sh
set -uo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
T="$ROOT/build/dvm-tests/t"
OUT="${OUT:-$ROOT/build/art-tests}"
export ANDROID_DATA="$OUT/android-data"
mkdir -p "$ANDROID_DATA" "$OUT/oat"
pass=0; fail=0
for d in "$T"/*/; do
    t="$(basename "$d")"
    (cd "$d" && timeout 600 dalvikvm -Xss8m -cp classes.dex "$t" > art.out 2> art.err)
    code=$?
    # ATL's runtime prints this line for an uncaught exception
    grep -v '^hacky_uncaught_exception_handler' "$d/art.out" > "$d/art.cmp"
    if cmp -s "$d/expected" "$d/art.cmp"; then
        printf '  ok    %-14s exit %d\n' "$t" $code; pass=$((pass + 1))
    else
        printf '  FAIL  %-14s exit %d\n' "$t" $code; fail=$((fail + 1))
        { diff "$d/expected" "$d/art.cmp" || true; } | head -10 | sed 's/^/        /'
        { grep -a -m5 -e 'Fatal' -e 'Exception' -e 'error' "$d/art.err" || true; } | sed 's/^/        stderr: /'
    fi
done
cache="${XDG_CACHE_HOME:-$HOME/.cache}/art"
# the apps' oat files, and the boot class path's (boot*.oat)
find "$cache" \( -name '*@classes.dex' -o -name '*boot*.oat' \) -exec cp {} "$OUT/oat/" \; 2>/dev/null
for f in "$OUT"/oat/*; do [ -f "$f" ] && { echo "==> $(basename "$f")"; readelf -h "$f" | grep -e Machine -e Type; readelf --dyn-syms -W "$f" | grep -o 'oat[a-z]*' | sort -u | tr '\n' ' '; echo; break; }; done
echo "==> $pass passed, $fail failed"
[ $fail -eq 0 ]
