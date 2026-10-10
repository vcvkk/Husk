# Flags memory accesses whose base register was last written as a 32-bit (W) value
# without the heap decode (orr xN, xN, #0x800000000) in between: on arm64 with the
# heap above 4 GiB such an address is wrong.
import re, sys, collections
ins_re = re.compile(r'^\s+([0-9a-f]+):\s+(\S+)\s*(.*)$')
fn_re = re.compile(r'^[0-9a-f]+ <(.*)>:$')
mem_re = re.compile(r'\[x(\d+)')
hits = collections.Counter(); examples = {}
fn = None; win = []
def dest(op, args):
    a = args.split(',')[0].strip()
    m = re.match(r'([wx])(\d+)$', a)
    if not m: return None
    if op.startswith(('st', 'cmp', 'cmn', 'tst', 'b', 'cb', 'tb', 'prfm')): return None
    return m.group(1), int(m.group(2))
for line in open(sys.argv[1]):
    m = fn_re.match(line)
    if m: fn = m.group(1); win = []; continue
    m = ins_re.match(line)
    if not m: continue
    addr, op, args = m.groups()
    args = args.split('//')[0]
    for mm in mem_re.finditer(args):
        r = int(mm.group(1))
        if r in (19, 29, 30, 31): continue
        for (pop, pargs, d) in reversed(win):
            if d and d[1] == r:
                if d[0] == 'w' and not pop.startswith('ldr') or (pop == 'ldr' and d[0] == 'w'):
                    key = (pop, op)
                    hits[key] += 1
                    examples.setdefault(key, []).append(f'{addr} {fn}: {pop} {pargs.strip()} -> {op} {args.strip()}')
                break
    d = dest(op, args)
    if op == 'orr' and '#0x800000000' in args: d = ('x', int(args.split(',')[0].strip()[1:]))
    win.append((op, args, d)); win = win[-24:]
    if op in ('b', 'ret', 'br') or op.startswith('b.'): win = []
for k, n in hits.most_common():
    print(n, k)
    for e in examples[k][:4]: print('    ', e[:220])
