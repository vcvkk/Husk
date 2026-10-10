// SPDX-License-Identifier: GPL-2.0-or-later
// What libartbase takes from the system on Linux and iOS lacks.

#include <libkern/OSCacheControl.h>
#include <stddef.h>

// What __builtin___clear_cache calls: libSystem has it on macOS, not on iOS. On arm64
// sys_icache_invalidate cleans the data cache to the point of unification and
// invalidates the instruction cache, which is what the builtin promises.
extern "C" void __clear_cache(void* begin, void* end) {
  sys_icache_invalidate(begin, static_cast<char*>(end) - static_cast<char*>(begin));
}
