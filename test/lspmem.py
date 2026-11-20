#!/usr/bin/env python3
# usage: lspmem.py [binary]
# Measure the language server's memory across a few requests, as the
# footprint Activity Monitor shows.
import sys, os, json, subprocess, re, time

HERE = os.path.dirname(os.path.abspath(__file__))
URBIT = os.path.join(HERE, 'urbit/pkg')
binary = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, 'hp-fast')
proc = subprocess.Popen([binary], stdin=subprocess.PIPE, stdout=subprocess.PIPE)

def request(i, method, params):
    body = json.dumps({'jsonrpc': '2.0', 'id': i, 'method': method, 'params': params}).encode()
    proc.stdin.write(b'Content-Length: %d\r\n\r\n' % len(body) + body)
    proc.stdin.flush()
    n = 0
    while True:
        l = proc.stdout.readline().strip()
        if not l:
            break
        n = int(l.split(b':', 1)[1])
    return json.loads(proc.stdout.read(n))

def footprint():
    out = subprocess.run(['footprint', str(proc.pid)], capture_output=True, text=True).stdout
    m = re.search(r'Footprint: ([\d.]+ [KMG]B)', out)
    return m.group(1) if m else '?'

request(1, 'initialize', {})
print('%-10s %s' % ('start', footprint()))
for i, (f, l, c) in enumerate([('arvo/sys/vane/behn.hoon', 109, 7),
                               ('arvo/sys/vane/ames.hoon', 2000, 10),
                               ('arvo/app/dojo.hoon', 300, 10)]):
    uri = 'file://' + os.path.join(URBIT, f)
    t = time.time()
    request(10 + i, 'textDocument/semanticTokens/full', {'textDocument': {'uri': uri}})
    ms = ['%.0f' % ((time.time() - t) * 1000)]
    for j in range(3):
        t = time.time()
        request(20 + i, 'textDocument/definition',
                {'textDocument': {'uri': uri}, 'position': {'line': l, 'character': c}})
        ms.append('%.0f' % ((time.time() - t) * 1000))
    print('%-10s %-8s  tokens %s ms, definition %s ms' % (os.path.basename(f), footprint(), ms[0], ' '.join(ms[1:])))
request(99, 'shutdown', None)
proc.stdin.write(b'Content-Length: 33\r\n\r\n{"jsonrpc":"2.0","method":"exit"}')
proc.stdin.close()
proc.wait()
