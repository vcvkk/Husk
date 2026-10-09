# The DVM

A Dalvik VM in C, for running an Android app's DEX bytecode in-process on
iOS. It continues the idea of `src/translation-layer/husk-tl-dex.c` (which ran
Flappy Bird) with what that interpreter lacked: virtual and interface
dispatch, exceptions, class initialisation that follows Java's rules, and a
garbage collector.

| File | What |
|---|---|
| `dvm.h` | The public API: create a VM, add DEX files, run `main` |
| `dvm-dex.c` | DEX parsing, checked against the file's size; descriptor → class_def index |
| `dvm-class.c` | Loading, field layout, vtables, `<clinit>`, resolution of DEX references |
| `dvm-interp.c` | The interpreter: every Dalvik opcode up to `0xe2` |
| `dvm-heap.c` | Allocation and the collector |
| `dvm-native.c` | Strings, exceptions, the VM's life cycle, the boot library's native methods |
| `bootlib/` | The Java class library the VM boots with, until libcore |

## Values

Every register and field slot is 64 bits. Ints and floats sit in the low 32
bits with the high 32 zero; longs and doubles take all 64 in the first register
of their pair; references are pointers. Because ints are always zero-extended,
`if-eq` compares the full 64 bits and is right for both ints and references.

## Dispatch

A class's vtable is its superclass's with overrides replaced in place and new
virtual methods appended, followed by interface methods it does not implement
(default methods, or abstract ones that throw `AbstractMethodError`).
`invoke-virtual` indexes the receiver's vtable; `invoke-interface` looks the
method up in it by name and proto and caches the answer for the last receiver
class; `invoke-super` uses the calling method's class's superclass's vtable,
or calls an interface's default method directly.

## Exceptions

A throw sets `vm->exception`. The interpreter never advances `pc` before an
instruction that can throw has succeeded, so the handler search uses the
throwing instruction's own address. With no handler, the method returns
false and its caller searches. A handler entered keeps the exception in its
frame for `move-exception`. `StackOverflowError` is thrown at 3000 frames, with
headroom left to construct it.

## The collector

Non-moving mark-sweep. Each object is its own allocation, recorded in a hash
set, which is how the collector recognises pointers when it scans the register
stack and the C stack conservatively. Everything else is precise: static
fields, instance fields through each class's list of reference slots, arrays
of references, resolved string constants, class objects, the pending
exception and the ones handlers hold. `DVM_GC_STRESS=N` collects every N
allocations, which is how rooting mistakes are found.

## Not yet

- More than one thread. Monitors check for null and otherwise do nothing.
- `invoke-polymorphic`, `invoke-custom`, `const-method-handle`,
  `const-method-type`: they throw `UnsupportedOperationException`. d8
  desugars lambdas, so ordinary code does not use them.
- JNI, weak references, finalizers, reflection beyond `Class` basics,
  regular expressions and formatting in the boot library.
- Speed: a `switch` loop, one C call per Java call.
