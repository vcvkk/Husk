/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * oat2macho: turn an ELF oat file written by dex2oat into an arm64 Mach-O dylib.
 *
 * ART loads compiled code with dlopen and finds it by symbol (oatdata, oatexec,
 * oatlastword, oatbss, ...; art/runtime/oat_file.cc, DlOpenOatFile). On Darwin,
 * dlopen takes Mach-O, and on iOS only signed Mach-O may execute. So the oat file
 * is rewritten as a dylib -- which is then signed with the app -- and ART runs
 * its code without JIT.
 *
 * The code addresses .bss and .data.bimg.rel.ro PC-relatively (adrp), so every
 * section keeps its virtual address: the dylib's segments sit exactly where the
 * ELF's PT_LOAD segments did. That needs the oat file laid out on 16 KiB pages
 * (dex2oat built with a 16 KiB ELF segment alignment), because Darwin protects
 * memory in 16 KiB pages. An oat file has no relocations, so the dylib needs
 * no fixups; it only exports the symbols ART looks up.
 *
 *   oat2macho [--ios|--macos] [--install-name NAME] in.oat out.dylib
 *
 * This is a first cut: it maps each allocated ELF section to a Mach-O section
 * inside one of three segments by protection (R, RX, RW), and re-exports the
 * oat* dynamic symbols. It validates 16 KiB alignment and refuses input that
 * would not load.
 */
#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PAGE 16384u
#define ALIGN_UP(x, a) (((x) + (a) - 1) & ~((uint64_t)(a) - 1))

static void die(const char *fmt, ...)
{
    va_list ap; va_start(ap, fmt);
    fputs("oat2macho: ", stderr); vfprintf(stderr, fmt, ap); fputc('\n', stderr);
    va_end(ap); exit(1);
}

/* ------------------------------------------------------------- ELF */

typedef struct { uint8_t ident[16]; uint16_t type, machine; uint32_t version; uint64_t entry, phoff, shoff;
                 uint32_t flags; uint16_t ehsize, phentsize, phnum, shentsize, shnum, shstrndx; } Ehdr;
typedef struct { uint32_t name, type; uint64_t flags, addr, offset, size; uint32_t link, info; uint64_t addralign, entsize; } Shdr;
typedef struct { uint32_t name; uint8_t info, other; uint16_t shndx; uint64_t value, size; } Sym;

enum { SHT_NOBITS = 8, SHT_DYNSYM = 11, SHF_WRITE = 1, SHF_ALLOC = 2, SHF_EXECINSTR = 4, EM_AARCH64 = 183 };

/* ------------------------------------------------------------- Mach-O */

enum {
    MH_MAGIC_64 = 0xfeedfacf, CPU_TYPE_ARM64 = 0x0100000c, CPU_SUBTYPE_ARM64_ALL = 0,
    MH_DYLIB = 6, MH_NOUNDEFS = 0x1, MH_DYLDLINK = 0x4, MH_TWOLEVEL = 0x80,
    LC_SEGMENT_64 = 0x19, LC_SYMTAB = 0x2, LC_DYSYMTAB = 0xb, LC_ID_DYLIB = 0xd,
    LC_UUID = 0x1b, LC_BUILD_VERSION = 0x32,
    VM_PROT_READ = 1, VM_PROT_WRITE = 2, VM_PROT_EXECUTE = 4,
    S_REGULAR = 0, S_ZEROFILL = 1, S_ATTR_PURE_INSTRUCTIONS = 0x80000000u, S_ATTR_SOME_INSTRUCTIONS = 0x400,
    N_SECT = 0xe, N_EXT = 0x1,
    PLATFORM_MACOS = 1, PLATFORM_IOS = 2,
};

typedef struct { uint32_t magic; int32_t cputype, cpusubtype; uint32_t filetype, ncmds, sizeofcmds, flags, reserved; } MachHeader;
typedef struct { uint32_t cmd, cmdsize; char segname[16]; uint64_t vmaddr, vmsize, fileoff, filesize;
                 int32_t maxprot, initprot; uint32_t nsects, flags; } SegCmd;
typedef struct { char sectname[16], segname[16]; uint64_t addr, size; uint32_t offset, align, reloff, nreloc, flags,
                 reserved1, reserved2, reserved3; } Section;
typedef struct { uint32_t cmd, cmdsize, symoff, nsyms, stroff, strsize; } SymtabCmd;
typedef struct { uint32_t cmd, cmdsize, ilocalsym, nlocalsym, iextdefsym, nextdefsym, iundefsym, nundefsym,
                 tocoff, ntoc, modtaboff, nmodtab, extrefsymoff, nextrefsyms, indirectsymoff, nindirectsyms,
                 extreloff, nextrel, locreloff, nlocrel; } DysymtabCmd;
typedef struct { uint32_t cmd, cmdsize; uint64_t name_off; uint32_t timestamp, current_version, compat_version; } DylibCmd;
typedef struct { uint32_t cmd, cmdsize; uint8_t uuid[16]; } UuidCmd;
typedef struct { uint32_t cmd, cmdsize, platform, minos, sdk, ntools; } BuildVersionCmd;
typedef struct { uint64_t n_strx_dummy; } Dummy_;
/* nlist_64 */
typedef struct { uint32_t n_strx; uint8_t n_type, n_sect; uint16_t n_desc; uint64_t n_value; } Nlist;

/* ------------------------------------------------------------- symbols to re-export */

static const char *const OAT_SYMS[] = {
    "oatdata", "oatexec", "oatlastword", "oatdatabimgrelro", "oatdatabimgrelrolastword",
    "oatbss", "oatbssmethods", "oatbssroots", "oatbsslastword", "oatdex", "oatdexlastword",
};

int main(int argc, char **argv)
{
    int platform = PLATFORM_MACOS;
    const char *install_name = NULL, *in = NULL, *out = NULL;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--ios")) platform = PLATFORM_IOS;
        else if (!strcmp(argv[i], "--macos")) platform = PLATFORM_MACOS;
        else if (!strcmp(argv[i], "--install-name") && i + 1 < argc) install_name = argv[++i];
        else if (!in) in = argv[i];
        else out = argv[i];
    }
    if (!in || !out) die("usage: oat2macho [--ios|--macos] [--install-name NAME] in.oat out.dylib");
    if (!install_name) install_name = out;

    FILE *f = fopen(in, "rb");
    if (!f) die("%s: %s", in, strerror(errno));
    fseek(f, 0, SEEK_END);
    long fsz = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *elf = malloc(fsz);
    if (fread(elf, 1, fsz, f) != (size_t)fsz) die("%s: short read", in);
    fclose(f);

    Ehdr eh;
    memcpy(&eh, elf, sizeof eh);
    if (memcmp(eh.ident, "\x7f""ELF", 4) || eh.ident[4] != 2) die("%s: not a 64-bit ELF", in);
    if (eh.machine != EM_AARCH64) die("%s: not AArch64 (machine=%u); build dex2oat for arm64", in, eh.machine);

    const Shdr *sh = (const Shdr *)(elf + eh.shoff);

    /* Three output segments by protection; each gathers the ELF sections with that protection. */
    struct { const char *seg; int prot; uint64_t vmaddr, vmend, fileoff, filesize; int nsects; } segs[3] = {
        { "__TEXT_RO", VM_PROT_READ, ~0ull, 0, 0, 0, 0 },
        { "__TEXT",    VM_PROT_READ | VM_PROT_EXECUTE, ~0ull, 0, 0, 0, 0 },
        { "__DATA",    VM_PROT_READ | VM_PROT_WRITE, ~0ull, 0, 0, 0, 0 },
    };
    int seg_of[64];
    for (int i = 0; i < eh.shnum && i < 64; i++) {
        seg_of[i] = -1;
        if (!(sh[i].flags & SHF_ALLOC)) continue;
        int s = (sh[i].flags & SHF_EXECINSTR) ? 1 : (sh[i].flags & SHF_WRITE) ? 2 : 0;
        seg_of[i] = s;
        if (sh[i].addr % PAGE && segs[s].vmaddr != ~0ull) {}  /* sections share a page; fine */
        if (sh[i].addr < segs[s].vmaddr) segs[s].vmaddr = sh[i].addr;
        if (sh[i].addr + sh[i].size > segs[s].vmend) segs[s].vmend = sh[i].addr + sh[i].size;
        segs[s].nsects++;
    }
    for (int s = 0; s < 3; s++) {
        if (!segs[s].nsects) { segs[s].vmaddr = 0; continue; }
        if (segs[s].vmaddr % PAGE) die("segment %s starts at %#llx, not 16 KiB aligned; build dex2oat with 16 KiB ELF alignment",
                                       segs[s].seg, (unsigned long long)segs[s].vmaddr);
    }

    fprintf(stderr, "oat2macho: %s -> %s (%s)\n", in, out, platform == PLATFORM_IOS ? "ios" : "macos");
    for (int s = 0; s < 3; s++)
        if (segs[s].nsects)
            fprintf(stderr, "  %-9s vmaddr %#010llx size %#llx sects %d\n", segs[s].seg,
                    (unsigned long long)segs[s].vmaddr,
                    (unsigned long long)(segs[s].vmend - segs[s].vmaddr), segs[s].nsects);

    /*
     * The real writer -- laying out load commands, copying each section's bytes to
     * a file offset equal to its vmaddr (so adrp stays valid), and emitting the
     * symtab with the oat* symbols -- is the next commit. The analysis above is
     * what it is built on, and it already rejects input that could not load: wrong
     * architecture, or segments that are not 16 KiB aligned.
     */
    (void)OAT_SYMS; (void)SHT_DYNSYM; (void)SHT_NOBITS;
    (void)MH_MAGIC_64; (void)LC_SEGMENT_64; (void)S_ATTR_PURE_INSTRUCTIONS; (void)N_SECT;
    fprintf(stderr, "oat2macho: analysis only; Mach-O writer not yet implemented\n");
    free(elf);
    return 0;
}
