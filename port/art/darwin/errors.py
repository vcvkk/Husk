#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Summarise a `ninja -k 0` log: how many steps failed, and each distinct error once."""
import collections
import re
import sys

text = open(sys.argv[1], errors="replace").read()
failed = re.findall(r"^FAILED: (\S+)", text, re.M)
errors = collections.OrderedDict()
for m in re.finditer(r"^(\S+?):(\d+):\d+: (?:fatal )?error: (.*)$", text, re.M):
    f, line, msg = m.groups()
    f = re.sub(r"^.*?/art_standalone/", "", f)
    errors.setdefault((f, msg), line)
undefined = sorted(set(re.findall(r'"(_[^"]+)", referenced from', text)))
print(f"{len(failed)} steps failed, {len(errors)} distinct compile errors, {len(undefined)} undefined symbols at link")
by_file = collections.Counter(f for f, _ in errors)
print("\nfiles with most errors:")
for f, n in by_file.most_common(40):
    print(f"  {n:4d}  {f}")
print("\nerrors:")
for (f, msg), line in list(errors.items())[:400]:
    print(f"  {f}:{line}: {msg}")
if undefined:
    print("\nundefined symbols:")
    for s in undefined[:300]:
        print("  " + s)
