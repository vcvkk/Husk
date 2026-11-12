# SPDX-License-Identifier: GPL-2.0-or-later
"""
An lldb stop hook for the macOS test host (.github/workflows/extera.yml): lldb stops at each EXC_BAD_ACCESS; the ones in ART's
compiled code (boot*.oat.dylib, *.odex.dylib: its implicit null and stack checks, which ART's fault handler answers) go on to
the process, and any other stops it there with a backtrace whose frames in an Android library are named by the loader
(tl_ld_symbol_at), as ART's own report on Darwin has no native stack.

    lldb -o 'command script import tools/lldb_crash_hook.py' -o 'target stop-hook add -P lldb_crash_hook.CrashHook' ...
"""
import lldb

ART_CODE = ('.oat.dylib', '.odex.dylib')


def guest_name(frame, pc):
    v = frame.EvaluateExpression('(const char *)tl_ld_lib_name((void *)tl_ld_lib_of((const void *)%#x))' % pc)
    lib = v.GetSummary() if v.IsValid() and v.GetValueAsUnsigned() else None
    if not lib:
        return None
    v = frame.EvaluateExpression('(const char *)tl_ld_symbol_at((const void *)%#x, (const char **)0, (const void **)0)' % pc)
    sym = v.GetSummary() if v.IsValid() and v.GetValueAsUnsigned() else None
    return '%s %s' % (lib.strip('"'), (sym or '?').strip('"'))


class CrashHook:
    def __init__(self, target, extra_args, internal_dict):
        self.passed = 0

    def handle_stop(self, exe_ctx, stream):
        t = exe_ctx.GetThread()
        if not t.IsValid() or t.GetStopReason() != lldb.eStopReasonException:
            return True
        f0 = t.GetFrameAtIndex(0)
        mod = f0.GetModule().GetFileSpec().GetFilename() or ''
        if mod.endswith(ART_CODE):
            self.passed += 1
            return False
        stream.Print('crash hook: %s on thread %d "%s" (%d faults in ART code passed before)\n'
                     % (t.GetStopDescription(200), t.GetIndexID(), t.GetName() or '', self.passed))
        for i in range(min(t.GetNumFrames(), 40)):
            f = t.GetFrameAtIndex(i)
            pc = f.GetPC()
            m = f.GetModule().GetFileSpec().GetFilename() or '?'
            name = f.GetFunctionName() or f.GetSymbol().GetName() or ''
            guest = guest_name(f0, pc) if (m.startswith('aot-') or m == '?' or not name) else None
            stream.Print('  #%02d %#x %s %s%s\n' % (i, pc, m, name, ('  [%s]' % guest) if guest else ''))
        return True
