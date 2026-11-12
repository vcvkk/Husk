#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Builds tests/plugins/husk_bot_login.plugin: husk_bot_login.py.in with BotLogin's dex in it.
#   build.sh R8_JAR
# javac against stubs/ (the app's classes it uses, their signatures only), d8 for the dex.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
R8="${1:?usage: build.sh r8.jar}"
TMP="$(mktemp -d)"; trap 'rm -rf "$TMP"' EXIT
javac -nowarn --release 8 -d "$TMP/stubs" $(find "$HERE/stubs" -name '*.java')
javac -nowarn --release 8 -cp "$TMP/stubs" -d "$TMP/classes" $(find "$HERE/src" -name '*.java')
java -cp "$R8" com.android.tools.r8.D8 --release --min-api 24 --classpath "$TMP/stubs" --output "$TMP" $(find "$TMP/classes" -name '*.class')
python3 - "$HERE/husk_bot_login.py.in" "$TMP/classes.dex" "$HERE/../husk_bot_login.plugin" <<'PY'
import base64, sys
src, dex, out = sys.argv[1:]
open(out, "w").write(open(src).read().replace("@DEX@", base64.b64encode(open(dex, "rb").read()).decode()))
PY
echo "$HERE/../husk_bot_login.plugin"
