#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""
oat-strip-code.py file.oat...: an oat file and its Mach-O copy (<file.oat>.dylib, oat2macho) each
keeping only what is read of it, before the copy is signed.

On Apple systems ART maps an oat file's code from <oat file>.dylib (patch 0008) and reads the ELF
for the rest: its headers, .rodata (the oat data) and .dynsym. oat2macho keeps every section's file
offset, so the dylib has the oat data too and the ELF has the code: each was there twice. Here the
ELF's code is zeroed, and in the dylib everything between its load commands and the code; zeroed
bytes compress to nothing in an .ipa. The files keep their sizes and layouts.
"""
import os
import struct
import sys

PAGE = 16384


def zero(f, offset, size):
    f.seek(offset)
    while size > 0:
        n = min(size, 1 << 20)
        f.write(b"\0" * n)
        size -= n


def code_segment(f):
    """the ELF's executable PT_LOAD: (file offset, size), or None"""
    ehdr = f.read(64)
    if ehdr[:4] != b"\x7fELF" or ehdr[4] != 2:
        return None
    phoff, = struct.unpack_from("<Q", ehdr, 32)
    phentsize, phnum = struct.unpack_from("<HH", ehdr, 54)
    for i in range(phnum):
        f.seek(phoff + i * phentsize)
        p_type, p_flags, p_offset, _, _, p_filesz = struct.unpack("<IIQQQQ", f.read(40))
        if p_type == 1 and p_flags & 1 and p_filesz:          # PT_LOAD, PF_X
            return p_offset, p_filesz
    return None


for path in sys.argv[1:]:
    with open(path, "r+b") as f:
        seg = code_segment(f)
        if not seg:
            sys.exit(f"{path}: no code segment (not a 64-bit ELF oat file?)")
        off, size = seg
        dylib = path + ".dylib"
        if os.path.exists(dylib):
            with open(dylib, "r+b") as d:
                mh = d.read(32)
                magic, = struct.unpack_from("<I", mh, 0)
                if magic != 0xFEEDFACF:
                    sys.exit(f"{dylib}: not a 64-bit Mach-O file")
                sizeofcmds, = struct.unpack_from("<I", mh, 20)
                start = (32 + sizeofcmds + PAGE - 1) // PAGE * PAGE
                if start < off:
                    zero(d, start, off - start)
                print(f"{dylib}: {max(off - start, 0)} bytes of oat data zeroed")
        zero(f, off, size)
        print(f"{path}: {size} bytes of code zeroed")
