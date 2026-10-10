#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""
Rewrite ART's preprocessed arm64 assembly from ELF conventions to Mach-O ones.

ART's runtime assembly (art/runtime/arch/arm64/*.S, the generated mterp_arm64.S)
was only ever assembled for ELF. For Darwin:

  - C symbols carry a leading underscore: artFoo is _artFoo, and the entry points
    C++ declares extern "C" (art_quick_*) must be defined as _art_quick_*;
  - .type and .size do not exist; .hidden is .private_extern;
  - GOT-relative addressing is sym@GOTPAGE / sym@GOTPAGEOFF, not :got: / :got_lo12:;
  - private labels start with L, not .L;
  - macro arguments are separated by commas: Darwin's parser runs
    space-separated ones together into the first parameter;
  - a non-private label may not sit inside .cfi_startproc/.cfi_endproc, which
    mterp's global handler-table labels do: with --no-cfi, CFI is dropped.

Symbols are recognised where they can be told apart from labels, registers and
macro names: .extern/.global/.hidden operands, non-local label definitions,
branch and address targets, and ART's own name prefixes, which is how names
passed to assembler macros as arguments are found. Rewriting a label in all its
uses keeps references within the file consistent either way.

  asm-elf2macho.py [--no-cfi] in.s out.s     (in.s: the output of clang -E on a .S file)
"""
import re
import sys

REG = re.compile(r"^(?:[xwvqdshb]\d{1,2}|sp|wsp|xzr|wzr|lr|fp|ip[01]|pc|nzcv|fpcr|fpsr|tpidr_el0|lsl|lsr|asr|ror|"
                 r"uxt[bhwx]|sxt[bhwx]|eq|ne|cs|hs|cc|lo|mi|pl|vs|vc|hi|ls|ge|lt|gt|le|al|nv)$", re.I)
PREFIX = re.compile(r"^(?:art[A-Z_]\w*|Art\w+|Mterp\w+|mterp\w+|Execute\w+Impl\w*|ExecuteSwitchImplAsm|MterpHelper\w*|NterpGetMethod\w*|_Z\w+)$")
IDENT = r"[A-Za-z_][A-Za-z0-9_$]*"


def split_args(text):
    """Space-separated macro arguments, keeping expressions like (512 - 8) whole."""
    args, depth = [], 0
    ops = "+-*/|&^<>~!="
    for tok in text.split():
        joined = args and (depth > 0 or tok[0] in ops or args[-1][-1] in ops)
        if joined:
            args[-1] += " " + tok
        else:
            args.append(tok)
        depth += tok.count("(") - tok.count(")")
    return args


def main():
    files = [a for a in sys.argv[1:] if not a.startswith("--")]
    text = open(files[0]).read()
    lines = text.split("\n")

    macros = set(re.findall(r"^\s*\.macro\s+(" + IDENT + ")", text, re.M))
    syms = set()
    for m in re.finditer(r"^\s*\.(?:extern|global|globl|hidden|private_extern|weak)\s+(" + IDENT + ")", text, re.M):
        syms.add(m.group(1))
    for m in re.finditer(r"^\s*(" + IDENT + r")\s*:", text, re.M):
        syms.add(m.group(1))
    for m in re.finditer(r"\b(?:bl|b|blr|adr|adrp)\s+(" + IDENT + r")\b", text):
        syms.add(m.group(1))
    for tok in set(re.findall(r"(?<![\\.\w])(" + IDENT + r")\b", text)):
        if PREFIX.match(tok):
            syms.add(tok)
    syms = {s for s in syms if not REG.match(s) and s not in macros and not s.startswith(".")}

    no_cfi = "--no-cfi" in sys.argv
    out = []
    for line in lines:
        if no_cfi and re.match(r"^\s*\.cfi_", line):
            continue
        # macro invocations: "NAME a b c" -> "NAME a, b, c"
        m = re.match(r"^(\s*)(" + IDENT + r")(\s+)(.*?)\s*$", line)
        if m and m.group(2) in macros and "," not in m.group(4).split("//")[0]:
            args = split_args(m.group(4).split("//")[0])
            if len(args) > 1:
                line = m.group(1) + m.group(2) + " " + ", ".join(args)
        s = line.strip()
        if re.match(r"^\.(type|size)\b", s):
            continue
        line = re.sub(r"^(\s*)\.hidden\b", r"\1.private_extern", line)
        line = re.sub(r":got:(" + IDENT + ")", r"\1@GOTPAGE", line)
        line = re.sub(r":got_lo12:(" + IDENT + ")", r"\1@GOTPAGEOFF", line)
        # rename symbols, but not inside comments or strings, nor macro parameters (\name)
        code, sep, comment = line.partition("//")
        code = re.sub(r"(?<![\\.\w$])(" + IDENT + r")\b",
                      lambda m: "_" + m.group(1) if m.group(1) in syms else m.group(1), code)
        code = re.sub(r"(?<![\w$])\.L(\w)", r"L\1", code)     # private labels: .Lfoo -> Lfoo
        out.append(code + sep + comment)
    open(files[1], "w").write("\n".join(out))


if __name__ == "__main__":
    main()
