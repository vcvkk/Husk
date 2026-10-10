#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""
Write a build.ninja that builds ART's runtime for Darwin (macOS or iOS, arm64)
from modules.json (extract.py), with clang.

Only what runs an app: dalvikvm, libart and what it loads, the class library's
native code. Compilation happens ahead of time on Linux, so the compiler and
dex2oat are left out.

  gen-ninja.py --src ART_TREE --out BUILD_DIR [--target macos|ios] [--sysroot SDK]
               [--include DIR]... [--lib DIR]... modules.json

Libraries keep their Linux names (libart.so, ...): dlopen does not care about the
extension, and the runtime has the names written into it.
"""
import argparse
import json
import os
import shlex

RUNTIME = ["dalvikvm", "libart", "libjavacore", "libopenjdk", "libopenjdkjvm"]
SKIP = {"libunwind", "libbacktrace", "libwolfssljni", "libart-compiler", "libart-dexlayout",
        "dex2oat", "libart-dex2oat.a", "acp", "libhost.a", "libsigchain"}
# system libraries on Linux -> what to link on Darwin (None: in libSystem, or not needed)
SYSTEM = {"c": None, "gcc": None, "pthread": None, "rt": None, "dl": None, "atomic": None, "bsd": None,
          "z": "z", "lz4": "lz4", "lzma": "lzma", "expat": "expat", "crypto": "crypto",
          "icuuc": "icuuc", "icui18n": "icui18n", "dl_bio": "dl_bio", "vixl": None}

# Linux lets a shared library leave symbols for whatever loads it; Darwin's linker wants each
# one found. Libraries the build does not name, and symbols of a cycle (libcutils links
# libutils, whose NativeHandle calls libcutils), resolved at load time.
EXTRA_LIBS = {"libutils": ["liblog"]}
LOOKUP = {"libutils": ["_native_handle_close", "_native_handle_delete"]}

DROP = {"-Wa,--noexecstack", "-fpermissive", "-ffloat-store", "-Werror", "-no-canonical-prefixes",
        "-DLINUX", "-D__GLIBC__", "-m64", "-fstack-protector", "-g", "-ggdb3"}


def translate(args, darwin_config):
    out, it = [], iter(args)
    for a in it:
        if a in ("-I", "-isystem", "-include"):
            v = next(it, "")
            if a == "-include" and "linux-any/AndroidConfig.h" in v:
                v = darwin_config
            out += [a, v]
            continue
        if a in DROP or a.startswith("-W") and not a.startswith("-Wno"):
            continue
        if a.startswith("-Wno"):
            continue
        if a == "-fvisibility=protected":
            a = "-fvisibility=default"
        if a == "-std=gnu++23":
            a = "-std=gnu++2b"
        if a in ("-I/usr/include/vixl", "-I/usr/local/include/vixl", "-I/usr/include/valgrind/"):
            continue
        out.append(a)
    if "-DLINUX" in args:                       # OpenJDK's native code: the BSD/macOS branches
        out += ["-D_ALLBSD_SOURCE", "-DMACOSX"]
    return out


def needed(modules):
    seen, todo = set(), list(RUNTIME)
    while todo:
        n = todo.pop()
        if n in seen or n in SKIP or n not in modules:
            continue
        seen.add(n)
        link = modules[n]["link"]
        todo += link["libs"] + link["whole_static"] + link["static"]
    return seen


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--src", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--target", default="macos", choices=["macos", "ios"])
    ap.add_argument("--sysroot")
    ap.add_argument("--min-os")
    ap.add_argument("--include", action="append", default=[])
    ap.add_argument("--lib", action="append", default=[])
    ap.add_argument("--extra-src", action="append", default=[], help="module:file added to a module")
    ap.add_argument("--ldflag", action="append", default=[], help="added to every link")
    ap.add_argument("manifest")
    o = ap.parse_args()

    data = json.load(open(o.manifest))
    modules, generated, flagsets = data["modules"], data["generated"], data["flagsets"]
    for m in modules.values():
        for ob in m["objects"]:
            ob["args"] = flagsets[ob["flags"]]
    src, out = os.path.abspath(o.src), os.path.abspath(o.out)
    compat = os.path.abspath(os.path.join(os.path.dirname(__file__), "compat"))
    darwin_config = os.path.join(compat, "AndroidConfig.h")
    triple = "arm64-apple-ios" + (o.min_os or "14.0") if o.target == "ios" else "arm64-apple-macos" + (o.min_os or "11.0")
    base = ["-target", triple, "-w", "-g"] + (["-isysroot", o.sysroot] if o.sysroot else [])
    # the Linux headers and spellings Darwin lacks come first (compat/)
    base += [f"-I{compat}"] + [f"-I{d}" for d in o.include]
    # gcc lets these through as warnings; clang 15 made them errors
    cflags_c = ["-Wno-error=implicit-function-declaration", "-Wno-error=incompatible-function-pointer-types",
                "-Wno-error=int-conversion", "-Wno-error=incompatible-pointer-types"]
    forced = ["-include", os.path.join(compat, "darwin_compat.h")]
    ldbase = ["-target", triple] + (["-isysroot", o.sysroot] if o.sysroot else []) + [f"-L{d}" for d in o.lib] + o.ldflag
    want = needed(modules)
    q = lambda xs: " ".join(shlex.quote(x) for x in xs)
    gen = lambda p: p.replace("out/host/linux-x86/", out + "/")

    w = open(os.path.join(out, "build.ninja"), "w")
    w.write(f"""# generated by port/art/darwin/gen-ninja.py
src = {src}
rule cc
  command = cd $src && clang {q(base + forced + cflags_c)} $flags -MD -MF $out.d -c $in -o $out
  depfile = $out.d
  deps = gcc
  description = CC $in
rule cxx
  command = cd $src && clang++ {q(base + forced)} $flags -MD -MF $out.d -c $in -o $out
  depfile = $out.d
  deps = gcc
  description = CXX $in
rule asm
  command = cd $src && clang {q(base)} $flags -E -x assembler-with-cpp $in -o $out.pre.s && python3 {os.path.abspath(os.path.join(os.path.dirname(__file__), "asm-elf2macho.py"))} $asmopts $out.pre.s $out.s && clang {q(base)} -c $out.s -o $out
  description = ASM $in
rule ar
  command = rm -f $out && ar rcs $out $in
  description = AR $out
rule so
  command = clang++ {q(ldbase)} -dynamiclib -install_name @rpath/$soname -Wl,-rpath,@loader_path -o $out $in $ldflags
  description = LINK $out
rule exe
  command = clang++ {q(ldbase)} -Wl,-rpath,@executable_path -Wl,-rpath,@executable_path/../lib -o $out $in $ldflags
  description = LINK $out
rule gen
  command = cd $src && $cmd
  description = GEN $out
""")

    # generators: python ones run anywhere; asm_defines is compiled for the target
    gen_outputs = {}
    for g in generated:
        target_out = gen(g["out"])
        mod_dir = g["out"].split("/")[-2] if "_intermediates" in g["out"] else ""
        if not any(f"/{m}_intermediates/" in g["out"] for m in want):
            continue
        if g["tool"] == "compile-S":
            cmd = f"clang++ {q(base)} -S {q(translate(g['args'], darwin_config))} {g['src']} -o {target_out}"
        else:
            args = [gen(a) for a in g["args"]]
            cmd = f"python3 {g['tool']} {q(args)}" + (f" > {target_out}" if not g["tool"].endswith("gen_mterp.py") else "")
            if g["tool"].endswith("gen_mterp.py"):
                cmd = f"python3 {g['tool']} {target_out} {' '.join(args)}"
        deps = [gen_outputs[a] for a in [gen(x) for x in g["args"]] if a in gen_outputs]
        w.write(f"build {target_out}: gen | {' '.join(deps)}\n  cmd = mkdir -p {os.path.dirname(target_out)} && {cmd}\n")
        gen_outputs[target_out] = target_out
        # the build copies generated files from gen/ to obj/; refer to both
        gen_outputs[target_out.replace('/gen/', '/obj/')] = target_out

    all_gen = sorted(set(gen_outputs.values()))
    products = {}
    for name in sorted(want):
        m = modules[name]
        objs = []
        sources = list(m["objects"])
        for extra in o.extra_src:
            mname, f = extra.split(":", 1)
            if mname == name:
                proto = next((x for x in m["objects"] if x["lang"] == ("c" if f.endswith(".c") else "c++")), m["objects"][0])
                sources.append({"src": os.path.abspath(f), "lang": "c" if f.endswith(".c") else "c++", "args": proto["args"]})
        for ob in sources:
            s = ob["src"]
            s_abs = gen(s) if s.startswith("out/") else s if os.path.isabs(s) else os.path.join(src, s)
            if s.startswith("out/"):
                if s_abs not in gen_outputs:
                    continue
                s_abs = gen_outputs[s_abs]           # the build compiles obj/ copies of gen/ files
            # the source's own path names the object: two files of a module may share a basename
            obj = os.path.join(out, "obj", name, s.replace("out/host/linux-x86/", "").replace("/", "_") + ".o")
            rule = {"c": "cc", "c++": "cxx", "asm": "asm"}[ob["lang"]]
            flags = [gen(a) for a in translate(ob["args"], darwin_config)]
            mine = [g for g in all_gen if f"/{name}_intermediates/" in g]
            w.write(f"build {obj}: {rule} {s_abs} || {' '.join(mine)}\n  flags = {q(flags)}\n")
            if rule == "asm":
                # global labels inside their CFI regions, which Mach-O does not allow
                nocfi = any(k in s for k in ("mterp_arm64", "quick_entrypoints_arm64"))
                w.write(f"  asmopts = {'--no-cfi' if nocfi else ''}\n")
            objs.append(obj)
        if m["kind"] == "static":
            p = os.path.join(out, "lib", name)
            w.write(f"build {p}: ar {' '.join(objs)}\n")
        else:
            products[name] = (objs, m)
        if m["kind"] == "static":
            products[name] = p
    for name, val in products.items():
        if isinstance(val, str):
            continue
        objs, m = val
        link = m["link"]
        ld, deps = [], []
        for s in link["whole_static"]:
            if s in want:
                ld.append(f"-Wl,-force_load,{products[s]}")
                deps.append(products[s])
        for s in link["static"]:
            if s in want and s not in link["whole_static"]:
                ld.append(products[s])
                deps.append(products[s])
        for l in link["libs"] + EXTRA_LIBS.get(name, []):
            if l in want:
                p = os.path.join(out, "lib", l + ".so")
                ld.append(p)
                deps.append(p)
        for l in link["system"]:
            d = SYSTEM.get(l, l)
            if d:
                ld.append("-l" + d)
        ld += [f"-Wl,-U,{s}" for s in LOOKUP.get(name, [])]
        if m["kind"] == "shared":
            p = os.path.join(out, "lib", name + ".so")
            w.write(f"build {p}: so {' '.join(objs)} | {' '.join(deps)}\n  soname = {name}.so\n  ldflags = {q(ld)}\n")
        else:
            p = os.path.join(out, "bin", name)
            w.write(f"build {p}: exe {' '.join(objs)} | {' '.join(deps)}\n  ldflags = {q(ld)}\n")
    w.write(f"default {' '.join(os.path.join(out, 'bin', n) if modules[n]['kind'] == 'exe' else os.path.join(out, 'lib', n + '.so') for n in RUNTIME if n in want)}\n")
    w.close()
    print(f"{len(want)} modules: {' '.join(sorted(want))}")


if __name__ == "__main__":
    main()
