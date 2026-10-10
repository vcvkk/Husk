#!/usr/bin/env bash
# Runs tests/dvm's programs on the installed ART (dalvikvm) and compares their
# output with the host JVM's, as tests/dvm/run.sh recorded it. dalvikvm compiles
# each one ahead of time with dex2oat for the machine's own architecture, so on
# an arm64 host this exercises arm64 AOT code. The compiled files are copied to
# $OUT/oat for inspection.
#
#   tests/dvm/run.sh && port/art/test.sh
#   DALVIKVM_FLAGS="-Xint -Xnoimage-dex2oat" port/art/test.sh    (interpreter, no boot image)
set -uo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
T="$ROOT/build/dvm-tests/t"
OUT="${OUT:-$ROOT/build/art-tests}"
export ANDROID_DATA="$OUT/android-data"
mkdir -p "$ANDROID_DATA" "$OUT/oat"
pass=0; fail=0
MAX_FAIL="${MAX_FAIL:-3}"   # after this many failures the rest are skipped: CI time
for d in "$T"/*/; do
    t="$(basename "$d")"
    if [ $fail -ge "$MAX_FAIL" ]; then
        printf '  skip  %s\n' "$t"; continue
    fi
    # a stuck runtime gets SIGQUIT first, on which ART dumps every thread's stack, then KILL
    (cd "$d" && timeout -s QUIT -k 15 120 dalvikvm ${DALVIKVM_FLAGS:-} -Xss8m -cp classes.dex "$t" \
        > art.out 2> art.err)
    code=$?
    # ATL's runtime prints this line for an uncaught exception
    grep -v '^hacky_uncaught_exception_handler' "$d/art.out" > "$d/art.cmp"
    if cmp -s "$d/expected" "$d/art.cmp"; then
        printf '  ok    %-14s exit %d\n' "$t" $code; pass=$((pass + 1))
    else
        printf '  FAIL  %-14s exit %d\n' "$t" $code; fail=$((fail + 1))
        { diff "$d/expected" "$d/art.cmp" || true; } | head -10 | sed 's/^/        /'
        { grep -a -m5 -e 'Fatal' -e 'Exception' -e 'error' "$d/art.err" || true; } | sed 's/^/        stderr: /'
        # repeated lines once, then the end of it (a SIGQUIT dump, or a crash's report)
        [ $fail -le 2 ] && uniq -c "$d/art.err" | tail -${ERR_LINES:-120} | sed 's/^/        stderr| /'
    fi
done
cache="${XDG_CACHE_HOME:-$HOME/.cache}/art"
# the apps' oat files, and the boot class path's (boot*.oat)
find "$cache" \( -name '*@classes.dex' -o -name '*boot*.oat' \) -exec cp {} "$OUT/oat/" \; 2>/dev/null
for f in "$OUT"/oat/*; do [ -f "$f" ] && { echo "==> $(basename "$f")"; readelf -h "$f" | grep -e Machine -e Type; readelf --dyn-syms -W "$f" | grep -o 'oat[a-z]*' | sort -u | tr '\n' ' '; echo; break; }; done
echo "==> $pass passed, $fail failed"
[ $fail -eq 0 ]
