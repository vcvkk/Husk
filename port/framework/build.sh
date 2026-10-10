#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Builds the Android framework apps run on (android.*, husk.*) into a jar of dex:
#
#   port/framework/build.sh OUT.jar
#
# The framework is Husk's main line's (Leviidev/Husk src/java-framework, at 5ab4f00):
# Activity, View, resources, a Canvas over CoreGraphics and CoreText and the rest in
# Java, whose natives are port/framework/jni (JNI here, for ART). It compiles with javac
# against the JDK, with stubs/ for what libcore has and the JDK does not, then d8.
# Needs JAVA_HOME's javac or javac on PATH, and R8_JAR (port/art/build.sh fetches it).
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
OUT="$(realpath -m "${1:?usage: build.sh OUT.jar}")"
R8="${R8_JAR:?R8_JAR: r8.jar}"
JAVAC="${JAVA_HOME:+$JAVA_HOME/bin/}javac"
JAVA="${JAVA_HOME:+$JAVA_HOME/bin/}java"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
# GLES20's Java from its spec (the C the generator writes too is the interpreter's, not used here)
python3 "$HERE/gen/gles20.py" "$HERE/src/android/opengl" "$TMP/gles20-unused.c" > /dev/null
"$JAVAC" -nowarn --release 8 -encoding UTF-8 -d "$TMP/stubs" $(find "$HERE/stubs" -name '*.java')
if ! "$JAVAC" -nowarn --release 8 -encoding UTF-8 -Xmaxerrs 100000 -cp "$TMP/stubs" -d "$TMP/classes" \
        $(find "$HERE/src" "$HERE/gen-src" -name '*.java') 2> "$TMP/javac.log"; then
    grep -v "^Note:" "$TMP/javac.log" | head -60 >&2; echo "javac failed" >&2; exit 1
fi
(cd "$TMP/classes" && jar cf "$TMP/classes.jar" .)
# the class files too, for checking an app's references against the framework (check-app.sh)
[ -n "${CLASSES_JAR:-}" ] && cp "$TMP/classes.jar" "$CLASSES_JAR"
mkdir -p "$TMP/dex"
"$JAVA" -cp "$R8" com.android.tools.r8.D8 --min-api 26 --output "$TMP/dex" "$TMP/classes.jar"
mkdir -p "$(dirname "$OUT")"
rm -f "$OUT"
(cd "$TMP/dex" && jar cf "$OUT" classes*.dex)
ls -la "$OUT"
