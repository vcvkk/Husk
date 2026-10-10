// SPDX-License-Identifier: GPL-2.0-or-later
// Can a process on this Darwin system get memory below 4 GiB?
//
// ART 10 keeps references between Java objects as 32-bit pointers, so its heap
// and boot image must be mapped below 4 GiB. A 64-bit Mach-O program has a
// __PAGEZERO segment, 4 GiB by default, over exactly that range. This reports
// the program's __PAGEZERO and tries to map memory below 4 GiB, by hint and at
// a fixed address. Built once as is and once with a small __PAGEZERO
// (-Wl,-pagezero_size,0x4000), it shows whether the kernel lets the second run.
#include <errno.h>
#include <mach-o/dyld.h>
#include <mach-o/loader.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>

static void pagezero(void) {
  const struct mach_header_64* mh = (const struct mach_header_64*)_dyld_get_image_header(0);
  const uint8_t* p = (const uint8_t*)(mh + 1);
  for (uint32_t i = 0; i < mh->ncmds; i++) {
    const struct load_command* lc = (const struct load_command*)p;
    if (lc->cmd == LC_SEGMENT_64) {
      const struct segment_command_64* seg = (const struct segment_command_64*)lc;
      if (strcmp(seg->segname, "__PAGEZERO") == 0)
        printf("__PAGEZERO: 0x%llx bytes\n", (unsigned long long)seg->vmsize);
    }
    p += lc->cmdsize;
  }
  printf("main image at %p (slide 0x%lx)\n", (const void*)mh, (long)_dyld_get_image_vmaddr_slide(0));
}

static void try_map(const char* how, void* addr, int flags) {
  size_t len = 64 << 20;
  void* r = mmap(addr, len, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON | flags, -1, 0);
  if (r == MAP_FAILED) {
    printf("%-24s %p: failed, %s\n", how, addr, strerror(errno));
    return;
  }
  *(volatile int*)r = 1;
  printf("%-24s %p: got %p %s\n", how, addr, r, (uintptr_t)r + len <= (1ull << 32) ? "(below 4 GiB)" : "(above 4 GiB)");
  munmap(r, len);
}

int main(void) {
  pagezero();
  try_map("hint", (void*)0x10000000, 0);
  try_map("hint", (void*)0x70000000, 0);
  try_map("fixed", (void*)0x10000000, MAP_FIXED);
  try_map("fixed", (void*)0x70000000, MAP_FIXED);
  try_map("anywhere", NULL, 0);
  return 0;
}
