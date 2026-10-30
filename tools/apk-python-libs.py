#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""
The native libraries an app's Python unpacks at run time, out of its APK: Chaquopy's (assets/chaquopy: bootstrap-native/<abi>/,
and the .so files in its stdlib-<abi>.imy and requirements-<abi>.imy zips), and those in any other zip of the assets named for
the ABI (exteraGram's plugins SDK, Cython modules: assets/plugins_pysdk/sdk-<abi>.zip). Without executable memory each must be a dylib made
ahead of time (tools/so2dylib.c), found by its contents' hash when Python loads it.

    apk-python-libs.py app.apk OUT_DIR [ABI]

Each library goes to OUT_DIR/<n>/<its name>, one directory each (packages have libraries of the same name); their paths are
printed, one a line.
"""
import io
import os
import sys
import zipfile

apk, out = sys.argv[1], sys.argv[2]
abi = sys.argv[3] if len(sys.argv) > 3 else "arm64-v8a"
os.makedirs(out, exist_ok=True)
n = 0


def put(name, data):
    global n
    d = os.path.join(out, str(n))
    os.makedirs(d, exist_ok=True)
    p = os.path.join(d, os.path.basename(name))
    with open(p, "wb") as f:
        f.write(data)
    n += 1
    print(p)


with zipfile.ZipFile(apk) as z:
    for e in z.namelist():
        if not e.startswith("assets/"):
            continue
        if e.startswith("assets/chaquopy/bootstrap-native/%s/" % abi) and e.endswith(".so"):
            put(e, z.read(e))
        elif e.endswith(("-%s.imy" % abi, "-%s.zip" % abi)):
            with zipfile.ZipFile(io.BytesIO(z.read(e))) as inner:
                for m in inner.namelist():
                    if m.endswith(".so"):
                        put(m, inner.read(m))
