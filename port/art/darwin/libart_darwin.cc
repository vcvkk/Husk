// SPDX-License-Identifier: GPL-2.0-or-later
// What libart takes from libraries the Darwin build leaves out.

#include <backtrace/BacktraceMap.h>

// libbacktrace reads /proc/<pid>/maps and unwinds with libunwind, for native stacks
// in thread dumps: without it, dumps show managed frames only.
BacktraceMap* BacktraceMap::Create(pid_t, bool) {
  return nullptr;
}
