#!/usr/bin/env python3
# usage: showtokens.py FILE [FROM TO]
# Ask the language server for semantic tokens and print the lines with
# each token colored by kind.
import sys, os, json, subprocess

HERE = os.path.dirname(os.path.abspath(__file__))
path = os.path.realpath(sys.argv[1])
lo, hi = (int(sys.argv[2]), int(sys.argv[3])) if len(sys.argv) > 3 else (1, 40)

proc = subprocess.Popen([os.path.join(HERE, 'hp-fast')], stdin=subprocess.PIPE, stdout=subprocess.PIPE)
def send(m):
    b = json.dumps(m).encode()
    proc.stdin.write(b'Content-Length: %d\r\n\r\n' % len(b) + b)
    proc.stdin.flush()
def recv():
    n = 0
    while True:
        l = proc.stdout.readline().strip()
        if not l:
            break
        k, v = l.split(b':', 1)
        n = int(v)
    return json.loads(proc.stdout.read(n))

send({'jsonrpc': '2.0', 'id': 1, 'method': 'initialize', 'params': {}})
legend = recv()['result']['capabilities']['semanticTokensProvider']['legend']['tokenTypes']
send({'jsonrpc': '2.0', 'id': 2, 'method': 'textDocument/semanticTokens/full',
      'params': {'textDocument': {'uri': 'file://' + path}}})
data = recv()['result']['data']
send({'jsonrpc': '2.0', 'id': 3, 'method': 'shutdown'})
recv()
send({'jsonrpc': '2.0', 'method': 'exit'})

colors = {'comment': 90, 'string': 32, 'number': 35, 'keyword': 31, 'function': 34,
          'variable': 0, 'type': 36, 'enumMember': 33, 'operator': 93}
lines = open(path, encoding='utf-8').read().split('\n')
marks = {}
line = col = 0
for i in range(0, len(data), 5):
    dl, dc, ln, ty, _ = data[i:i+5]
    line += dl
    col = dc if dl else col + dc
    marks.setdefault(line, []).append((col, ln, legend[ty]))
for n in range(lo - 1, min(hi, len(lines))):
    s = lines[n]
    u = s.encode('utf-16-le')
    out, last = '', 0
    for c, ln, ty in sorted(marks.get(n, [])):
        out += u[last*2:c*2].decode('utf-16-le')
        out += '\x1b[%dm%s\x1b[0m' % (colors[ty], u[c*2:(c+ln)*2].decode('utf-16-le'))
        last = c + ln
    out += u[last*2:].decode('utf-16-le')
    print('%4d  %s' % (n + 1, out))
print('\n' + '  '.join('\x1b[%dm%s\x1b[0m' % (colors[k], k) for k in legend))
