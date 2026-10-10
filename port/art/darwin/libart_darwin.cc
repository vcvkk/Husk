// SPDX-License-Identifier: GPL-2.0-or-later
// What libart takes from libraries the Darwin build leaves out.

#include <backtrace/BacktraceMap.h>

// libbacktrace reads /proc/<pid>/maps and unwinds with libunwind, for native stacks
// in thread dumps: without it, dumps show managed frames only.
BacktraceMap* BacktraceMap::Create(pid_t, bool) {
  return nullptr;
}

#include <libkern/OSCacheControl.h>
#include <stddef.h>

// What __builtin___clear_cache calls: libSystem has it on macOS, not on iOS. On arm64
// sys_icache_invalidate cleans the data cache to the point of unification and
// invalidates the instruction cache, which is what the builtin promises.
extern "C" void __clear_cache(void* begin, void* end) {
  sys_icache_invalidate(begin, static_cast<char*>(end) - static_cast<char*>(begin));
}
