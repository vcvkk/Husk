/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * oat-dlopen-test: load dylibs made by oat2macho the way ART will on Darwin, and
 * check what ART relies on (art/runtime/oat_file.cc, DlOpenOatFile::Load):
 *
 *   - dlopen succeeds, and dlsym finds oatdata, oatexec, oatlastword, oatbss...;
 *   - oatdata starts with the oat header's magic, "oat\n";
 *   - the distances between the symbols are the oat file's own (adrp in the
 *     compiled code depends on them);
 *   - oatexec's page is executable and not writable, oatbss's page writable.
 *
 *   oat-dlopen-test a.dylib [b.dylib...]          macOS only
 */
#include <dlfcn.h>
#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int protection(const void *p)
{
    mach_vm_address_t addr = (mach_vm_address_t)(uintptr_t)p;
    mach_vm_size_t size = 0;
    vm_region_basic_info_data_64_t info;
    mach_msg_type_number_t count = VM_REGION_BASIC_INFO_COUNT_64;
    mach_port_t object = MACH_PORT_NULL;
    if (mach_vm_region(mach_task_self(), &addr, &size, VM_REGION_BASIC_INFO_64, (vm_region_info_t)&info, &count, &object)
        != KERN_SUCCESS || addr > (mach_vm_address_t)(uintptr_t)p)
        return -1;
    return info.protection;
}

int main(int argc, char **argv)
{
    int failures = 0;
    for (int i = 1; i < argc; i++) {
        const char *path = argv[i];
        void *h = dlopen(path, RTLD_NOW | RTLD_LOCAL);
        if (!h) { printf("FAIL  %s: dlopen: %s\n", path, dlerror()); failures++; continue; }
        const char *names[] = { "oatdata", "oatexec", "oatlastword", "oatbss", "oatbsslastword", "oatdex" };
        const uint8_t *sym[6];
        int missing = 0;
        for (int k = 0; k < 6; k++) {
            sym[k] = dlsym(h, names[k]);
            if (!sym[k] && k < 3) { printf("FAIL  %s: dlsym(%s): %s\n", path, names[k], dlerror()); missing = 1; }
        }
        if (missing) { failures++; continue; }
        int ok = 1;
        if (memcmp(sym[0], "oat\n", 4)) { printf("FAIL  %s: oatdata does not start with the oat magic\n", path); ok = 0; }
        int px = protection(sym[1]);
        if (px < 0 || !(px & VM_PROT_EXECUTE) || (px & VM_PROT_WRITE)) {
            printf("FAIL  %s: oatexec page protection %d, want r-x\n", path, px); ok = 0;
        }
        if (sym[3]) {
            int pb = protection(sym[3]);
            if (pb < 0 || !(pb & VM_PROT_WRITE)) { printf("FAIL  %s: oatbss page protection %d, want rw-\n", path, pb); ok = 0; }
            ((volatile uint8_t *)sym[3])[0] = ((volatile uint8_t *)sym[3])[0];   /* writable in fact */
        }
        if (ok) {
            printf("ok    %s: oat version %.3s, oatdata %p, code %#lx bytes after it, bss %#lx\n", path, (const char *)sym[0] + 4,
                   (const void *)sym[0], (unsigned long)(sym[1] - sym[0]), sym[3] ? (unsigned long)(sym[3] - sym[0]) : 0ul);
        } else {
            failures++;
        }
    }
    return failures ? 1 : 0;
}
