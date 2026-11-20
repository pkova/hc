#!/usr/bin/env python3
# usage: gdcheck.py [gotodef.txt]
# Check go to definition answers. Each line is
#   FILE LINE:COL -> FILE LINE:COL
# with paths relative to $URBIT (default urbit/pkg here, which pin.sh
# makes), or to
# this directory when they start with ./
import sys, os, subprocess

HERE = os.path.dirname(os.path.abspath(__file__))
URBIT = os.environ.get('URBIT', os.path.join(HERE, 'urbit/pkg'))

def full(p):
    return os.path.join(HERE, p[2:]) if p.startswith('./') else os.path.join(URBIT, p)

bad = total = 0
for line in open(sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, 'gotodef.txt')):
    line = line.split('#')[0].strip()
    if not line:
        continue
    src, at, _, dst, want = line.split()
    l, c = at.split(':')
    r = subprocess.run([os.path.join(HERE, 'hp-fast'), '-d', full(src), l, c],
                       capture_output=True, timeout=60)
    got = r.stdout.decode().strip()
    ok = False
    if r.returncode == 0:
        f, gl, gc = got.rsplit(':', 2)
        ok = os.path.realpath(f) == os.path.realpath(full(dst)) and '%s:%s' % (gl, gc) == want
    total += 1
    if not ok:
        bad += 1
        print('FAIL %s %s: want %s %s, got %s' % (src, at, dst, want, got or r.stderr.decode().strip()))
print('%d/%d ok' % (total - bad, total))
