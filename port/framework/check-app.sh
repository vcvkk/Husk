#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Which of an app's references to the platform the framework and class library do not have:
# R8's TraceReferences over the APK, against their class files. On Android those would link;
# on ART here each is a NoSuchMethodError, NoSuchFieldError or NoClassDefFoundError when reached.
#
#   port/framework/check-app.sh APP.apk FRAMEWORK_CLASSES.jar LIBCORE_CLASSES_DIR
#
# FRAMEWORK_CLASSES.jar: build.sh's class files (CLASSES_JAR=...); LIBCORE_CLASSES_DIR: ART's build,
# out/host/common/obj/JAVA_LIBRARIES. Prints the missing members and classes of android.* and
# java.*, one each, then a count of all.
set -uo pipefail
APK="$1"; FW="$2"; LC="$3"
R8="${R8_JAR:?R8_JAR: r8.jar}"
libs=(--lib "$FW")
for j in core-oj core-libart okhttp bouncycastle apache-xml apachehttp wolfssljni; do libs+=(--lib "$LC/$j-hostdex_intermediates/classes.jar"); done
out=$(mktemp)
java -cp "$R8" com.android.tools.r8.tracereferences.TraceReferences --check --source "$APK" "${libs[@]}" > "$out" 2>&1
grep -oE "Missing (class|method|field) [^(]*(\([^)]*\))?" "$out" | sed 's/^Missing //' | grep -E " (android|java|javax)\." | sort -u
echo "$(grep -c '^Missing\|^Error: Missing' "$out") missing references in all (optional libraries' included)"
rm -f "$out"
