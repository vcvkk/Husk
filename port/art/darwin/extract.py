#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""
Turn art_standalone's build into a manifest a Darwin build can be generated from.

art_standalone builds with AOSP's old make system, cleaned up for Linux only. Its
dry run (make -n -B, with HOST_ARCH=arm64 so the runtime's arm64 sources are the
ones chosen) prints every command; this reads them back into modules:

  modules.<name> = {
    kind:    shared | static | exe
    objects: [{src, lang, flags}]         flags: an index into flagsets, compile flags as given
    link:    {libs, whole_static, static, system}   other modules, and -l system libraries
  }
  generated = [{tool, args, out}]         generate_operator_out.py, gen_mterp.py, asm_defines

  extract.py dry-run.txt > modules.json
"""
import json
import re
import shlex
import sys

OUT = "out/host/linux-x86/"


def module_of(path):
    """out/host/linux-x86/obj/SHARED_LIBRARIES/libart_intermediates/... -> (SHARED_LIBRARIES, libart)"""
    m = re.search(r"obj/([A-Z_]+)/([^/]+)_intermediates/", path)
    return (m.group(1), m.group(2)) if m else (None, None)


KIND = {"SHARED_LIBRARIES": "shared", "STATIC_LIBRARIES": "static", "EXECUTABLES": "exe"}


def main():
    text = open(sys.argv[1], errors="replace").read()
    modules, generated = {}, []

    def mod(kind_dir, name):
        key = name if kind_dir != "STATIC_LIBRARIES" else name + ".a"
        return modules.setdefault(key, {"kind": KIND.get(kind_dir, "?"), "objects": [],
                                        "link": {"libs": [], "whole_static": [], "static": [], "system": []}})

    for line in text.split("\n"):
        line = line.strip()
        if not line:
            continue
        try:
            argv = shlex.split(line)
        except ValueError:
            continue
        if not argv:
            continue
        tool = argv[0].split("/")[-1]

        # generators
        if tool in ("generate_operator_out.py", "gen_mterp.py", "make_header.py") and ">" in line or tool == "gen_mterp.py":
            if tool == "gen_mterp.py":
                generated.append({"tool": argv[0], "args": argv[2:], "out": argv[1]})
            else:
                cmd, _, out = line.partition(">")
                parts = shlex.split(cmd)
                generated.append({"tool": parts[0], "args": parts[1:], "out": out.strip()})
            continue
        if tool in ("g++", "gcc", "clang", "clang++") and "-S" in argv and "asm_defines.cc" in line:
            o = argv[argv.index("-o") + 1]
            src = [a for a in argv if a.endswith("asm_defines.cc")][0]
            flags = [a for a in argv[1:] if a not in ("-S", src, "-o", o)]
            generated.append({"tool": "compile-S", "args": flags, "src": src, "out": o})
            continue
        if tool not in ("g++", "gcc", "clang", "clang++"):
            continue

        if "-c" in argv:
            o = argv[argv.index("-o") + 1]
            kind_dir, name = module_of(o)
            if not name:
                continue
            src = argv[-1]
            args = [a for i, a in enumerate(argv[1:], 1)
                    if a not in ("-c", "-o", src) and argv[i - 1] not in ("-o", "-MF") and a not in ("-MD",)]
            # drop the -MF/-o values themselves
            clean, skip = [], False
            for a in argv[1:]:
                if skip:
                    skip = False
                    continue
                if a in ("-o", "-MF"):
                    skip = True
                    continue
                if a in ("-c", "-MD", src):
                    continue
                clean.append(a)
            lang = "asm" if src.endswith(".S") else "c" if src.endswith(".c") else "c++"
            mod(kind_dir, name)["objects"].append({"src": src, "lang": lang, "args": clean})
            continue

        if "-o" in argv:
            o = argv[argv.index("-o") + 1]
            if "/obj/lib/" in o and o.endswith(".so"):
                kind_dir, name = "SHARED_LIBRARIES", o.split("/")[-1][:-3]
            elif "/EXECUTABLES/" in o:
                kind_dir, name = module_of(o)
            else:
                continue
            m = mod(kind_dir, name)
            whole = False
            for a in argv[1:]:
                if a == "-Wl,--whole-archive":
                    whole = True
                elif a == "-Wl,--no-whole-archive":
                    whole = False
                elif a.endswith(".a"):
                    _, sname = module_of(a)
                    if sname:
                        (m["link"]["whole_static"] if whole else m["link"]["static"]).append(sname + ".a")
                elif a.startswith("-l"):
                    lib = a[2:]
                    m["link"]["system"].append(lib)
            continue

    # a -l naming another module of the build is a module dependency, not a system library
    for m in modules.values():
        sysl = []
        for lib in m["link"]["system"]:
            if "lib" + lib in modules:
                m["link"]["libs"].append("lib" + lib)
            else:
                sysl.append(lib)
        m["link"]["system"] = sorted(set(sysl))
        for k in ("libs", "whole_static", "static"):
            m["link"][k] = list(dict.fromkeys(m["link"][k]))

    # most files of a module share their flags: keep each set once
    flagsets, index = [], {}
    for m in modules.values():
        for ob in m["objects"]:
            key = tuple(ob.pop("args"))
            if key not in index:
                index[key] = len(flagsets)
                flagsets.append(list(key))
            ob["flags"] = index[key]
    json.dump({"modules": modules, "generated": generated, "flagsets": flagsets}, sys.stdout, indent=0, sort_keys=True)


if __name__ == "__main__":
    main()
