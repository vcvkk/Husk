#!/usr/bin/env bash
# The DVM's tests: every tests/dvm/*.java is compiled against the boot library,
# run on the host JVM for the expected output and on dvm-run, and the two
# compared (stdout and exit status). Needs clang, a JDK (javac, java) and d8,
# which is fetched from Google's Maven repository the first time.
#
#   tests/dvm/run.sh [TestName...]        DVM_CC=gcc DVM_SANITIZE=1 to vary the build
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
OUT="${DVM_OUT:-$ROOT/build/dvm-tests}"
R8_VERSION=8.13.25
R8_JAR="${R8_JAR:-$OUT/r8-$R8_VERSION.jar}"
CC="${DVM_CC:-clang}"
export JAVA_TOOL_OPTIONS="${JAVA_TOOL_OPTIONS:-}"
mkdir -p "$OUT"

if [ ! -f "$R8_JAR" ]; then
    echo "==> fetching d8 (r8 $R8_VERSION)"
    curl -fsSL -o "$R8_JAR.part" "https://dl.google.com/android/maven2/com/android/tools/r8/$R8_VERSION/r8-$R8_VERSION.jar"
    mv "$R8_JAR.part" "$R8_JAR"
fi
d8() { java -cp "$R8_JAR" com.android.tools.r8.D8 --min-api 26 "$@" 2>&1 | grep -v '^Picked up' || true; }
javac_() { javac -nowarn -Xlint:-options -encoding UTF-8 -source 8 -target 8 "$@" 2>&1 | grep -v -e '^Picked up' -e '^Note:' || true; }

echo "==> building dvm-run"
CFLAGS=(-std=c11 -D_GNU_SOURCE -O1 -g -Wall -Wextra -Wno-unused-parameter -DHUSK_DVM_ZIP)
[ "${DVM_SANITIZE:-0}" = 1 ] && CFLAGS+=(-fsanitize=address,undefined -fno-omit-frame-pointer)
"$CC" "${CFLAGS[@]}" -I"$ROOT/src/dvm" "$ROOT"/src/dvm/*.c "$ROOT/src/translation-layer/husk-tl-zip.c" \
    "$ROOT/tools/dvm-run.c" -lz -lm -o "$OUT/dvm-run"

echo "==> building the boot library"
rm -rf "$OUT/boot-classes" "$OUT/boot" && mkdir -p "$OUT/boot-classes" "$OUT/boot" "$OUT/empty"
javac_ -bootclasspath "$OUT/empty" -d "$OUT/boot-classes" $(find "$ROOT/src/dvm/bootlib" -name '*.java')
(cd "$OUT/boot-classes" && d8 --output "$OUT/boot" $(find . -name '*.class'))

tests=("$@")
[ ${#tests[@]} -eq 0 ] && tests=($(cd "$ROOT/tests/dvm" && ls *.java | sed 's/\.java$//'))
pass=0; fail=0
for t in "${tests[@]}"; do
    d="$OUT/t/$t"
    rm -rf "$d" && mkdir -p "$d"
    javac_ -bootclasspath "$OUT/boot-classes" -d "$d" "$ROOT/tests/dvm/$t.java"
    (cd "$d" && d8 --output . $(find . -name '*.class'))
    set +e
    (cd "$d" && java -Xss8m -Dstdout.encoding=UTF-8 -Dstderr.encoding=UTF-8 -cp . "$t") > "$d/expected" 2> "$d/expected.err"; want=$?
    "$OUT/dvm-run" --stats -cp "$OUT/boot/classes.dex:$d/classes.dex" "$t" > "$d/actual" 2> "$d/actual.err"; got=$?
    set -e
    if [ $want -eq $got ] && cmp -s "$d/expected" "$d/actual"; then
        printf '  ok    %-14s %s\n' "$t" "$(grep -o 'objects=[0-9]*' "$d/actual.err" || true)"
        pass=$((pass + 1))
    else
        printf '  FAIL  %-14s exit %d, expected %d\n' "$t" $got $want
        { diff "$d/expected" "$d/actual" || true; } | head -20 | sed 's/^/        /'
        { grep -v '^dvm-stats' "$d/actual.err" || true; } | head -10 | sed 's/^/        stderr: /'
        fail=$((fail + 1))
    fi
done
echo "==> $pass passed, $fail failed"
[ $fail -eq 0 ]
