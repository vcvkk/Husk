# Scans disassembled arm64 oat code (llvm-objdump -d) for code that breaks with the heap
# above 4 GiB, where a reference is the low 32 bits of an address and a register's X view
# holds the address (orr xN, xN, #0x7c00000000 decodes it):
#   base   a memory access whose base was last written as a 32-bit value (not decoded)
#   xtest  a null test or compare on the X view of a decoded reference: never 0, and null
#          may be 0x7c00000000 there; it must test the W view
import re, sys, collections
ins_re = re.compile(r'^\s+([0-9a-f]+):\s+(\S+)\s*(.*)$')
fn_re = re.compile(r'^[0-9a-f]+ <(.*)>:$')
mem_re = re.compile(r'\[x(\d+)')
hits = collections.Counter(); examples = {}
def hit(key, text):
    hits[key] += 1; examples.setdefault(key, []).append(text)
def dest(op, args):
    a = args.split(',')[0].strip()
    m = re.match(r'([wx])(\d+)$', a)
    if not m or op.startswith(('st', 'cmp', 'cmn', 'tst', 'b', 'cb', 'tb', 'prfm', 'ccmp')): return None
    return m.group(1), int(m.group(2))
fn = None; win = []; decoded = set()
for line in open(sys.argv[1]):
    m = fn_re.match(line)
    if m: fn = m.group(1); win = []; decoded = set(); continue
    m = ins_re.match(line)
    if not m: continue
    addr, op, args = m.groups()
    args = args.split('//')[0].strip()
    where = f'{addr} {fn}: {op} {args}'
    for mm in mem_re.finditer(args):
        r = int(mm.group(1))
        if r in (19, 29, 30, 31): continue
        for (pop, pargs, d) in reversed(win):
            if d and d[1] == r:
                if d[0] == 'w':
                    hit(('base', pop, op), f'{where}   <- {pop} {pargs}')
                break
    regs = re.findall(r'\bx(\d+)\b', args)
    if op in ('cbz', 'cbnz') and regs and int(regs[0]) in decoded:
        hit(('xtest', op), where)
    if op in ('cmp', 'ccmp', 'cmn', 'tst') and any(int(r) in decoded for r in regs[:2]):
        hit(('xtest', op), where)
    d = dest(op, args)
    if op == 'orr' and '#0x7c00000000' in args:
        d = ('x', int(args.split(',')[0].strip()[1:])); decoded.add(d[1])
    elif d:
        srcs = re.findall(r'\bx(\d+)\b', args)[1:]
        if op == 'mov' and d[0] == 'x' and srcs and int(srcs[0]) in decoded: decoded.add(d[1])
        else: decoded.discard(d[1])
    win.append((op, args, d)); win = win[-24:]
    if op in ('b', 'ret', 'br') or op.startswith('b.'): win = []
    if op.startswith(('bl', 'blr')): decoded = set()
for k, n in hits.most_common():
    print(n, k)
    for e in examples[k][:int(__import__("os").environ.get("N","4"))]: print('    ', e[:230])
