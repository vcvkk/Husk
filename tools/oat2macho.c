/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * oat2macho: turn an ELF oat file written by dex2oat into a Mach-O dylib.
 *
 * ART loads compiled code with dlopen and finds it by symbol (oatdata, oatexec,
 * oatlastword, oatbss, ...; art/runtime/oat_file.cc, DlOpenOatFile). On Darwin,
 * dlopen takes Mach-O, and on iOS only signed Mach-O may execute. So each oat
 * file is rewritten as a dylib -- signed with the app -- and ART runs its code
 * without JIT.
 *
 * Compiled code reaches .bss and .data.bimg.rel.ro PC-relatively (adrp), so every
 * section keeps its virtual address, and its file offset too where it has bytes:
 *
 *   __TEXT  r-x  vmaddr 0: the Mach-O header, then .rodata (__const) and .text
 *   __DATA  rw-  .data.bimg.rel.ro (__bimg_rel_ro), then .bss and .dex, zero-filled
 *   __LINKEDIT   the export trie dyld's dlsym reads, and the symbol table
 *
 * Darwin protects memory in 16 KiB pages, so the oat file must be laid out on
 * them (dex2oat with kElfSegmentAlignment = 16 KiB, port/art/patches); input that
 * would put code and writable data in one page is refused. An oat file has no
 * relocations, so the dylib needs no fixups.
 *
 *   oat2macho [--ios|--macos] [--install-name NAME] in.oat out.dylib
 */
#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PAGE 16384u
#define ROUND(x) (((uint64_t)(x) + PAGE - 1) & ~(uint64_t)(PAGE - 1))

static void die(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    fputs("oat2macho: ", stderr);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
    exit(1);
}

/* ------------------------------------------------------------- ELF */

typedef struct { uint8_t ident[16]; uint16_t type, machine; uint32_t version; uint64_t entry, phoff, shoff;
                 uint32_t flags; uint16_t ehsize, phentsize, phnum, shentsize, shnum, shstrndx; } Ehdr;
typedef struct { uint32_t name, type; uint64_t flags, addr, offset, size; uint32_t link, info; uint64_t addralign, entsize; } Shdr;
typedef struct { uint32_t name; uint8_t info, other; uint16_t shndx; uint64_t value, size; } Sym;

enum { SHT_NOBITS = 8, SHT_DYNSYM = 11, EM_X86_64 = 62, EM_AARCH64 = 183 };

/* ------------------------------------------------------------- Mach-O */

enum {
    MH_MAGIC_64 = 0xfeedfacf, MH_DYLIB = 6,
    MH_NOUNDEFS = 0x1, MH_DYLDLINK = 0x4, MH_TWOLEVEL = 0x80, MH_NO_REEXPORTED_DYLIBS = 0x100000,
    LC_SEGMENT_64 = 0x19, LC_SYMTAB = 0x2, LC_DYSYMTAB = 0xb, LC_ID_DYLIB = 0xd, LC_UUID = 0x1b,
    LC_BUILD_VERSION = 0x32,
    N_SECT = 0xe, N_EXT = 0x1,
    PLATFORM_MACOS = 1, PLATFORM_IOS = 2,
};
#define LC_DYLD_INFO_ONLY 0x80000022u
#define S_REGULAR 0u
#define S_ZEROFILL 1u
#define S_CODE (0x80000000u | 0x400u)    /* S_ATTR_PURE_INSTRUCTIONS | S_ATTR_SOME_INSTRUCTIONS */

typedef struct { uint32_t magic; int32_t cputype, cpusubtype; uint32_t filetype, ncmds, sizeofcmds, flags, reserved; } MachHeader;
typedef struct { uint32_t cmd, cmdsize; char segname[16]; uint64_t vmaddr, vmsize, fileoff, filesize;
                 int32_t maxprot, initprot; uint32_t nsects, flags; } SegCmd;
typedef struct { char sectname[16], segname[16]; uint64_t addr, size; uint32_t offset, align, reloff, nreloc, flags,
                 reserved1, reserved2, reserved3; } Section;
typedef struct { uint32_t cmd, cmdsize, symoff, nsyms, stroff, strsize; } SymtabCmd;
typedef struct { uint32_t cmd, cmdsize, ilocalsym, nlocalsym, iextdefsym, nextdefsym, iundefsym, nundefsym,
                 tocoff, ntoc, modtaboff, nmodtab, extrefsymoff, nextrefsyms, indirectsymoff, nindirectsyms,
                 extreloff, nextrel, locreloff, nlocrel; } DysymtabCmd;
typedef struct { uint32_t cmd, cmdsize, rebase_off, rebase_size, bind_off, bind_size, weak_bind_off, weak_bind_size,
                 lazy_bind_off, lazy_bind_size, export_off, export_size; } DyldInfoCmd;
typedef struct { uint32_t cmd, cmdsize, name_offset, timestamp, current_version, compat_version; } DylibCmd;
typedef struct { uint32_t cmd, cmdsize; uint8_t uuid[16]; } UuidCmd;
typedef struct { uint32_t cmd, cmdsize, platform, minos, sdk, ntools; } BuildVersionCmd;
typedef struct { uint32_t n_strx; uint8_t n_type, n_sect; uint16_t n_desc; uint64_t n_value; } Nlist;

/* ------------------------------------------------------------- a byte buffer */

typedef struct { uint8_t *p; size_t n, cap; } Buf;

static void put(Buf *b, const void *d, size_t n)
{
    if (b->n + n > b->cap) {
        b->cap = (b->n + n) * 2 + 64;
        b->p = realloc(b->p, b->cap);
    }
    memcpy(b->p + b->n, d, n);
    b->n += n;
}
static void put_u8(Buf *b, uint8_t v) { put(b, &v, 1); }
static void put_uleb(Buf *b, uint64_t v)
{
    do { uint8_t c = v & 0x7f; v >>= 7; if (v) c |= 0x80; put_u8(b, c); } while (v);
}
static size_t uleb_len(uint64_t v) { size_t n = 1; while (v >>= 7) n++; return n; }

/* ------------------------------------------------------------- the export trie */

/*
 * One node per character: a valid trie, if not the most compact. dyld walks it
 * edge by edge, so a name that is a prefix of another ("_oatbss", "_oatbssroots")
 * must end on a node the longer one continues from, never on an edge of its own.
 */
typedef struct Node {
    bool terminal;
    uint64_t addr;
    char ch[64];
    struct Node *kid[64];
    int nkids;
    size_t off;
} Node;

static void trie_add(Node *root, const char *name, uint64_t addr)
{
    Node *n = root;
    for (const char *c = name; *c; c++) {
        int i = 0;
        while (i < n->nkids && n->ch[i] != *c) i++;
        if (i == n->nkids) {
            if (n->nkids == 64) die("trie node overflow");
            n->ch[i] = *c;
            n->kid[i] = calloc(1, sizeof(Node));
            n->nkids++;
        }
        n = n->kid[i];
    }
    n->terminal = true;
    n->addr = addr;
}

static size_t node_size(const Node *n)
{
    size_t t = n->terminal ? 1 + uleb_len(n->addr) : 0;     /* flags (0) and address */
    size_t s = uleb_len(t) + t + 1;
    for (int i = 0; i < n->nkids; i++) s += 2 + uleb_len(n->kid[i]->off);
    return s;
}

static void collect(Node *n, Node ***list, int *len, int *cap)
{
    if (*len == *cap) { *cap = *cap * 2 + 16; *list = realloc(*list, *cap * sizeof(Node *)); }
    (*list)[(*len)++] = n;
    for (int i = 0; i < n->nkids; i++) collect(n->kid[i], list, len, cap);
}

static void trie_write(Node *root, Buf *out)
{
    Node **list = NULL;
    int len = 0, cap = 0;
    collect(root, &list, &len, &cap);
    /* offsets depend on ULEB sizes, which depend on offsets: iterate until stable */
    for (bool changed = true; changed;) {
        changed = false;
        size_t off = 0;
        for (int i = 0; i < len; i++) {
            if (list[i]->off != off) { list[i]->off = off; changed = true; }
            off += node_size(list[i]);
        }
    }
    for (int i = 0; i < len; i++) {
        Node *n = list[i];
        if (n->terminal) {
            put_uleb(out, 1 + uleb_len(n->addr));
            put_u8(out, 0);                                  /* EXPORT_SYMBOL_FLAGS_KIND_REGULAR */
            put_uleb(out, n->addr);
        } else {
            put_u8(out, 0);
        }
        put_u8(out, (uint8_t)n->nkids);
        for (int k = 0; k < n->nkids; k++) {
            put_u8(out, (uint8_t)n->ch[k]);
            put_u8(out, 0);
            put_uleb(out, n->kid[k]->off);
        }
    }
    while (out->n % 8) put_u8(out, 0);
    free(list);
}

/* ------------------------------------------------------------- conversion */

typedef struct { const char *elf, *sect, *seg; uint32_t flags; int sh, out_index; } Want;

int main(int argc, char **argv)
{
    int platform = PLATFORM_IOS;
    const char *install_name = NULL, *in = NULL, *out = NULL;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--ios")) platform = PLATFORM_IOS;
        else if (!strcmp(argv[i], "--macos")) platform = PLATFORM_MACOS;
        else if (!strcmp(argv[i], "--install-name") && i + 1 < argc) install_name = argv[++i];
        else if (!in) in = argv[i];
        else if (!out) out = argv[i];
        else die("unexpected argument %s", argv[i]);
    }
    if (!in || !out) die("usage: oat2macho [--ios|--macos] [--install-name NAME] in.oat out.dylib");
    char default_name[1024];
    if (!install_name) {
        const char *s = strrchr(out, '/');
        snprintf(default_name, sizeof(default_name), "@rpath/%s", s ? s + 1 : out);
        install_name = default_name;
    }

    FILE *f = fopen(in, "rb");
    if (!f) die("%s: %s", in, strerror(errno));
    fseek(f, 0, SEEK_END);
    long fsz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (fsz < (long)sizeof(Ehdr)) die("%s: too small", in);
    uint8_t *elf = malloc((size_t)fsz);
    if (fread(elf, 1, (size_t)fsz, f) != (size_t)fsz) die("%s: short read", in);
    fclose(f);

    Ehdr eh;
    memcpy(&eh, elf, sizeof eh);
    if (memcmp(eh.ident, "\x7f" "ELF", 4) || eh.ident[4] != 2 || eh.ident[5] != 1) die("%s: not a little-endian 64-bit ELF", in);
    int32_t cputype, cpusubtype;
    if (eh.machine == EM_AARCH64) { cputype = 0x0100000c; cpusubtype = 0; }
    else if (eh.machine == EM_X86_64) {
        if (platform == PLATFORM_IOS) die("%s: x86_64 code cannot run on iOS", in);
        cputype = 0x01000007; cpusubtype = 3;
    } else die("%s: machine %u is neither arm64 nor x86_64", in, eh.machine);
    if (eh.shoff + (uint64_t)eh.shnum * sizeof(Shdr) > (uint64_t)fsz || eh.shstrndx >= eh.shnum) die("%s: bad section table", in);
    const Shdr *sh = (const Shdr *)(elf + eh.shoff);
    const char *shstr = (const char *)(elf + sh[eh.shstrndx].offset);

    Want want[] = {
        { ".rodata",           "__const",       "__TEXT", S_REGULAR,  -1, 0 },
        { ".text",             "__text",        "__TEXT", S_CODE,     -1, 0 },
        { ".data.bimg.rel.ro", "__bimg_rel_ro", "__DATA", S_REGULAR,  -1, 0 },
        { ".bss",              "__bss",         "__DATA", S_ZEROFILL, -1, 0 },
        { ".dex",              "__dex",         "__DATA", S_ZEROFILL, -1, 0 },
    };
    enum { W_RODATA, W_TEXT, W_BIMG, W_BSS, W_DEX, NW };
    int dynsym = -1;
    for (int i = 0; i < eh.shnum; i++) {
        const char *name = shstr + sh[i].name;
        for (int w = 0; w < NW; w++) if (!strcmp(name, want[w].elf)) want[w].sh = i;
        if (sh[i].type == SHT_DYNSYM) dynsym = i;
    }
    if (want[W_RODATA].sh < 0 || dynsym < 0) die("%s: no .rodata or .dynsym; not an oat file", in);

    /* every section where the ELF put it, each starting on a 16 KiB page */
    for (int w = 0; w < NW; w++) {
        if (want[w].sh < 0) continue;
        const Shdr *s = &sh[want[w].sh];
        if (s->addr % PAGE)
            die("%s at %#llx is not on a 16 KiB page: compile with dex2oat built with kElfSegmentAlignment = 16 KiB",
                want[w].elf, (unsigned long long)s->addr);
        if (s->type != SHT_NOBITS && s->offset != s->addr)
            die("%s: file offset %#llx differs from address %#llx", want[w].elf,
                (unsigned long long)s->offset, (unsigned long long)s->addr);
    }
    const Shdr *ro = &sh[want[W_RODATA].sh];
    uint64_t text_end = ro->addr + ro->size;
    if (want[W_TEXT].sh >= 0) {
        const Shdr *t = &sh[want[W_TEXT].sh];
        if (t->addr < text_end) die(".text below .rodata");
        text_end = t->addr + t->size;
    }
    uint64_t text_vmsize = ROUND(text_end);
    uint64_t data_start = 0, data_end = 0, data_file_end = 0;
    for (int w = W_BIMG; w < NW; w++) {
        if (want[w].sh < 0) continue;
        const Shdr *s = &sh[want[w].sh];
        if (s->addr < text_vmsize) die("%s at %#llx shares a 16 KiB page with code", want[w].elf, (unsigned long long)s->addr);
        if (!data_start || s->addr < data_start) data_start = s->addr;
        if (s->addr + s->size > data_end) data_end = s->addr + s->size;
        if (s->type != SHT_NOBITS && s->size) data_file_end = s->addr + s->size;
    }
    /* zero-filled sections must follow every file-backed one in their segment */
    for (int w = W_BIMG; w < NW; w++)
        if (want[w].sh >= 0 && sh[want[w].sh].type == SHT_NOBITS && sh[want[w].sh].size && sh[want[w].sh].addr < data_file_end)
            die("%s is zero-filled but below file-backed data", want[w].elf);

    /* section numbering, as Mach-O counts them (1-based, in load-command order) */
    int nsect_text = 0, nsect_data = 0, idx = 1;
    for (int w = 0; w < NW; w++) {
        if (want[w].sh < 0) continue;
        want[w].out_index = idx++;
        if (!strcmp(want[w].seg, "__TEXT")) nsect_text++; else nsect_data++;
    }

    /* ----- __LINKEDIT: symbols and the export trie */
    const Sym *syms = (const Sym *)(elf + sh[dynsym].offset);
    size_t nsyms_in = sh[dynsym].size / sizeof(Sym);
    const char *dynstr = (const char *)(elf + sh[sh[dynsym].link].offset);
    Node *root = calloc(1, sizeof(Node));
    Buf strtab = { 0 }, nl = { 0 };
    put(&strtab, " ", 2);                                    /* string index 0 means "no name" */
    int nexports = 0;
    for (size_t i = 1; i < nsyms_in; i++) {
        const char *name = dynstr + syms[i].name;
        if (strncmp(name, "oat", 3)) continue;
        int sect = 0;
        for (int w = 0; w < NW; w++) if (want[w].sh == syms[i].shndx) sect = want[w].out_index;
        if (!sect) die("symbol %s is in no mapped section", name);
        char mname[128];
        snprintf(mname, sizeof(mname), "_%s", name);         /* C symbols carry an underscore on Darwin */
        Nlist n = { (uint32_t)strtab.n, N_SECT | N_EXT, (uint8_t)sect, 0, syms[i].value };
        put(&strtab, mname, strlen(mname) + 1);
        put(&nl, &n, sizeof n);
        trie_add(root, mname, syms[i].value);
        nexports++;
    }
    if (!nexports) die("%s exports no oat symbols", in);
    while (strtab.n % 8) put_u8(&strtab, 0);
    Buf trie = { 0 };
    trie_write(root, &trie);

    uint64_t data_vmsize = data_start ? ROUND(data_end) - data_start : 0;
    uint64_t data_filesize = data_file_end ? ROUND(data_file_end) - data_start : 0;
    uint64_t linkedit_vmaddr = data_start ? data_start + data_vmsize : text_vmsize;
    uint64_t linkedit_fileoff = data_file_end ? data_start + data_filesize : text_vmsize;
    uint64_t trie_off = linkedit_fileoff, sym_off = trie_off + trie.n, str_off = sym_off + nl.n;
    uint64_t linkedit_size = trie.n + nl.n + strtab.n;

    /* ----- load commands */
    Buf lc = { 0 };
    int ncmds = 0;
    SegCmd text = { LC_SEGMENT_64, (uint32_t)(sizeof(SegCmd) + nsect_text * sizeof(Section)), "__TEXT",
                    0, text_vmsize, 0, text_vmsize, 5, 5, (uint32_t)nsect_text, 0 };
    put(&lc, &text, sizeof text); ncmds++;
    for (int w = 0; w < NW; w++) {
        if (want[w].sh < 0 || strcmp(want[w].seg, "__TEXT")) continue;
        const Shdr *s = &sh[want[w].sh];
        Section sc = { "", "__TEXT", s->addr, s->size, (uint32_t)s->addr, 14, 0, 0, want[w].flags, 0, 0, 0 };
        memcpy(sc.sectname, want[w].sect, strlen(want[w].sect));
        put(&lc, &sc, sizeof sc);
    }
    if (nsect_data) {
        SegCmd data = { LC_SEGMENT_64, (uint32_t)(sizeof(SegCmd) + nsect_data * sizeof(Section)), "__DATA",
                        data_start, data_vmsize, data_filesize ? data_start : 0, data_filesize, 3, 3, (uint32_t)nsect_data, 0 };
        put(&lc, &data, sizeof data); ncmds++;
        for (int w = 0; w < NW; w++) {
            if (want[w].sh < 0 || strcmp(want[w].seg, "__DATA")) continue;
            const Shdr *s = &sh[want[w].sh];
            bool zf = s->type == SHT_NOBITS;
            Section sc = { "", "__DATA", s->addr, s->size, zf ? 0 : (uint32_t)s->addr, 14, 0, 0,
                           zf ? S_ZEROFILL : S_REGULAR, 0, 0, 0 };
            memcpy(sc.sectname, want[w].sect, strlen(want[w].sect));
            put(&lc, &sc, sizeof sc);
        }
    }
    SegCmd le = { LC_SEGMENT_64, sizeof(SegCmd), "__LINKEDIT", linkedit_vmaddr, ROUND(linkedit_size),
                  linkedit_fileoff, linkedit_size, 1, 1, 0, 0 };
    put(&lc, &le, sizeof le); ncmds++;
    DyldInfoCmd di = { LC_DYLD_INFO_ONLY, sizeof di, 0, 0, 0, 0, 0, 0, 0, 0, (uint32_t)trie_off, (uint32_t)trie.n };
    put(&lc, &di, sizeof di); ncmds++;
    SymtabCmd st = { LC_SYMTAB, sizeof st, (uint32_t)sym_off, (uint32_t)nexports, (uint32_t)str_off, (uint32_t)strtab.n };
    put(&lc, &st, sizeof st); ncmds++;
    DysymtabCmd ds = { LC_DYSYMTAB, sizeof ds, 0, 0, 0, (uint32_t)nexports, (uint32_t)nexports, 0,
                       0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
    put(&lc, &ds, sizeof ds); ncmds++;
    size_t namelen = strlen(install_name) + 1, idsize = (sizeof(DylibCmd) + namelen + 7) & ~(size_t)7;
    DylibCmd id = { LC_ID_DYLIB, (uint32_t)idsize, sizeof(DylibCmd), 1, 0x10000, 0x10000 };
    put(&lc, &id, sizeof id);
    put(&lc, install_name, namelen);
    for (size_t i = sizeof(DylibCmd) + namelen; i < idsize; i++) put_u8(&lc, 0);
    ncmds++;
    UuidCmd uu = { LC_UUID, sizeof uu, { 0 } };
    uint64_t h1 = 0xcbf29ce484222325ull, h2 = 0x84222325cbf29ce4ull;
    for (long i = 0; i < fsz; i++) { h1 = (h1 ^ elf[i]) * 0x100000001b3ull; h2 = (h2 ^ elf[fsz - 1 - i]) * 0x100000001b3ull; }
    memcpy(uu.uuid, &h1, 8);
    memcpy(uu.uuid + 8, &h2, 8);
    uu.uuid[6] = (uu.uuid[6] & 0x0f) | 0x50;
    uu.uuid[8] = (uu.uuid[8] & 0x3f) | 0x80;
    put(&lc, &uu, sizeof uu); ncmds++;
    uint32_t minos = platform == PLATFORM_IOS ? 0x000e0000 : 0x000b0000;   /* iOS 14, macOS 11 */
    BuildVersionCmd bv = { LC_BUILD_VERSION, sizeof bv, (uint32_t)platform, minos, minos, 0 };
    put(&lc, &bv, sizeof bv); ncmds++;

    MachHeader mh = { MH_MAGIC_64, cputype, cpusubtype, MH_DYLIB, (uint32_t)ncmds, (uint32_t)lc.n,
                      MH_NOUNDEFS | MH_DYLDLINK | MH_TWOLEVEL | MH_NO_REEXPORTED_DYLIBS, 0 };
    /* a code signer adds LC_CODE_SIGNATURE: leave room for it */
    if (sizeof mh + lc.n + 64 > ro->addr) die("load commands do not fit below .rodata at %#llx", (unsigned long long)ro->addr);

    /* ----- the file */
    uint64_t total = linkedit_fileoff + linkedit_size;
    uint8_t *o = calloc(1, total);
    memcpy(o, &mh, sizeof mh);
    memcpy(o + sizeof mh, lc.p, lc.n);
    for (int w = 0; w < NW; w++) {
        if (want[w].sh < 0) continue;
        const Shdr *s = &sh[want[w].sh];
        if (s->type == SHT_NOBITS || !s->size) continue;
        if (s->offset + s->size > (uint64_t)fsz) die("%s extends past the end of the file", want[w].elf);
        memcpy(o + s->addr, elf + s->offset, s->size);
    }
    memcpy(o + trie_off, trie.p, trie.n);
    memcpy(o + sym_off, nl.p, nl.n);
    memcpy(o + str_off, strtab.p, strtab.n);

    FILE *of = fopen(out, "wb");
    if (!of) die("%s: %s", out, strerror(errno));
    if (fwrite(o, 1, total, of) != total) die("%s: write failed", out);
    fclose(of);
    fprintf(stderr, "oat2macho: %s -> %s: %s %s, text 0..%#llx, data %#llx..%#llx, %d symbols\n", in, out,
            eh.machine == EM_AARCH64 ? "arm64" : "x86_64", platform == PLATFORM_IOS ? "iOS" : "macOS",
            (unsigned long long)text_vmsize, (unsigned long long)data_start,
            (unsigned long long)(data_start + data_vmsize), nexports);
    free(o);
    free(elf);
    return 0;
}
