#!/usr/bin/env python3
# usage: lsptest.py [binary]
# Drive the language server through a session and check its answers.
import sys, os, json, subprocess

HERE = os.path.dirname(os.path.abspath(__file__))
URBIT = os.environ.get('URBIT', os.path.expanduser('~/Desktop/urbit/pkg'))
binary = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, 'hp-fast')

proc = subprocess.Popen([binary], stdin=subprocess.PIPE, stdout=subprocess.PIPE)

def send(msg):
    body = json.dumps(msg).encode()
    proc.stdin.write(b'Content-Length: %d\r\n\r\n' % len(body) + body)
    proc.stdin.flush()

def recv():
    length = 0
    while True:
        line = proc.stdout.readline().strip()
        if not line:
            break
        k, v = line.split(b':', 1)
        if k.lower() == b'content-length':
            length = int(v)
    return json.loads(proc.stdout.read(length))

# what the server says unasked: diagnostics, by uri
diagnostics = {}

def request(i, method, params):
    send({'jsonrpc': '2.0', 'id': i, 'method': method, 'params': params})
    while True:
        r = recv()
        if 'id' in r:
            return r
        if r.get('method') == 'textDocument/publishDiagnostics':
            diagnostics[r['params']['uri']] = r['params']['diagnostics']

# read until there are diagnostics for a uri, as they come when the
# server has nothing else to do
def waitdiagnostics(u):
    diagnostics.pop(u, None)
    while u not in diagnostics:
        r = recv()
        if r.get('method') == 'textDocument/publishDiagnostics':
            diagnostics[r['params']['uri']] = r['params']['diagnostics']
    return diagnostics[u]

def uri(path):
    return 'file://' + os.path.realpath(path)

bad = total = 0
def check(name, got, want):
    global bad, total
    total += 1
    if got != want:
        bad += 1
        print('FAIL %s:\n  want %s\n  got  %s' % (name, want, got))

# a kernel and base-dev for desks without their own sys/
basedev = os.path.join(URBIT, 'base-dev')
r = request(1, 'initialize', {'processId': None, 'rootUri': uri(HERE), 'capabilities': {},
                              'initializationOptions': {'sys': os.path.join(URBIT, 'arvo/sys'),
                                                        'deps': [basedev]}})
check('initialize', r['result']['capabilities']['definitionProvider'], True)
check('hover capability', r['result']['capabilities'].get('hoverProvider'), True)
send({'jsonrpc': '2.0', 'method': 'initialized', 'params': {}})

behn = os.path.join(URBIT, 'arvo/sys/vane/behn.hoon')
hoon = os.path.join(URBIT, 'arvo/sys/hoon.hoon')

def definition(i, path, line, ch):
    r = request(i, 'textDocument/definition',
                {'textDocument': {'uri': uri(path)}, 'position': {'line': line, 'character': ch}})
    res = r['result']
    if res is None:
        return None
    return (res['uri'], res['range']['start']['line'], res['range']['start']['character'])

# a file that isn't open: read from disk; weld is at 110:8, 1-based
check('weld from disk', definition(2, behn, 109, 7), (uri(hoon), 733, 4))

# an open document with unsaved changes: two lines added at the top
text = open(behn).read()
send({'jsonrpc': '2.0', 'method': 'textDocument/didOpen',
      'params': {'textDocument': {'uri': uri(behn), 'languageId': 'hoon', 'version': 1,
                                  'text': '::  one\n::  two\n' + text}}})
check('weld in edited document', definition(3, behn, 111, 7), (uri(hoon), 733, 4))
# state now at 47 (0-based line 46), sample of the gate
check('local face in edited document', definition(4, behn, 134, 19), (uri(behn), 46, 23))

# a change with a non-ascii comment before a wing on the same line: é is
# two bytes but one UTF-16 unit
lines = text.split('\n')
lines[109] = lines[109].replace('(weld', '(weld', 1)
lines.insert(109, '  ::  é')
edited = '\n'.join(lines)
send({'jsonrpc': '2.0', 'method': 'textDocument/didChange',
      'params': {'textDocument': {'uri': uri(behn), 'version': 2},
                 'contentChanges': [{'text': edited}]}})
check('after didChange', definition(5, behn, 110, 7), (uri(hoon), 733, 4))
indent = lines[110][:len(lines[110]) - len(lines[110].lstrip())]
lines[110] = indent + "=+  z='é𝄞'  " + lines[110].lstrip()
send({'jsonrpc': '2.0', 'method': 'textDocument/didChange',
      'params': {'textDocument': {'uri': uri(behn), 'version': 3},
                 'contentChanges': [{'text': '\n'.join(lines)}]}})
col = lines[110].index('(weld') + 1
col16 = len(lines[110][:col].encode('utf-16-le')) // 2
check('utf-16 columns', definition(6, behn, 110, col16), (uri(hoon), 733, 4))

# closed: back to the file on disk
send({'jsonrpc': '2.0', 'method': 'textDocument/didClose',
      'params': {'textDocument': {'uri': uri(behn)}}})
check('after didClose', definition(7, behn, 109, 7), (uri(hoon), 733, 4))

# an edited kernel file: the cached kernel is rebuilt from it
htext = open(hoon).read()
send({'jsonrpc': '2.0', 'method': 'textDocument/didOpen',
      'params': {'textDocument': {'uri': uri(hoon), 'languageId': 'hoon', 'version': 1,
                                  'text': '::  one\n::  two\n' + htext}}})
check('edited kernel', definition(11, behn, 109, 7), (uri(hoon), 735, 4))
send({'jsonrpc': '2.0', 'method': 'textDocument/didClose',
      'params': {'textDocument': {'uri': uri(hoon)}}})
check('closed kernel', definition(12, behn, 109, 7), (uri(hoon), 733, 4))

# highlighting: well formed tokens covering the file
r = request(20, 'textDocument/semanticTokens/full', {'textDocument': {'uri': uri(behn)}})
data = r['result']['data']
check('semantic tokens', len(data) > 1000 and len(data) % 5 == 0, True)
check('token types', max(data[3::5]) < 9, True)

# a desk without sys/, and its imports from base-dev
uses = os.path.join(HERE, 'nosys/app/uses.hoon')
dbug = os.path.realpath(os.path.join(basedev, 'lib/dbug.hoon'))
check('configured sys', definition(30, uses, 5, 3), (uri(hoon), 733, 4))
check('configured deps', definition(31, uses, 6, 12), ('file://' + dbug, 21, 4))
# settings from the editor replace the deps; relative to the workspace
send({'jsonrpc': '2.0', 'method': 'workspace/didChangeConfiguration', 'params': {'settings': {'deps': []}}})
check('deps removed', definition(32, uses, 6, 12), None)
send({'jsonrpc': '2.0', 'method': 'workspace/didChangeConfiguration',
      'params': {'settings': {'deps': [os.path.relpath(basedev, HERE)]}}})
check('relative deps', definition(33, uses, 6, 12), ('file://' + dbug, 21, 4))

# hover: the source of the definition as hoon, and where it is
def hover(i, path, line, ch):
    r = request(i, 'textDocument/hover',
                {'textDocument': {'uri': uri(path)}, 'position': {'line': line, 'character': ch}})
    res = r['result']
    return res and res['contents']['value']

h = hover(40, behn, 109, 7)
check('hover arm', h and h.startswith('```hoon\n++  weld') and h.endswith('```\nhoon.hoon:734'), True)
check('hover arm body', h and '  [i.a $(a t.a)]\n```' in h, True)
check('hover sample', hover(41, behn, 132, 19),
      '```hoon\n|=  [[now=@da =duct] state=behn-state]\n```\nbehn.hoon:45')
h = hover(42, uses, 1, 5)
check('hover import', h and h.startswith('```hoon\n::  dbug:') and h.endswith('\n...\n```\ndbug.hoon:1'), True)
h = hover(43, uses, 6, 12)
check('hover long arm', h and h.startswith('```hoon\n++  agent\n') and '\n...\n```' in h
      and h.count('\n') == 18, True)
check('hover nothing', hover(44, behn, 0, 0), None)

# diagnostics: an open document with an error in it, unsaved, and none
# once it's closed
nest = os.path.join(HERE, 'desk/app/nest.hoon')
send({'jsonrpc': '2.0', 'method': 'textDocument/didOpen',
      'params': {'textDocument': {'uri': uri(nest), 'languageId': 'hoon', 'version': 1,
                                  'text': '|=  a=@ud\n^-  @t\na\n'}}})
d = waitdiagnostics(uri(nest))
check('diagnostic', len(d) == 1 and d[0]['message'].startswith('nest-fail\n  need: @t\n  have: @ud'), True)
check('diagnostic range', d and d[0]['range'], {'start': {'line': 2, 'character': 0}, 'end': {'line': 2, 'character': 1}})
# a nest-fail with a type made by a mold: where the mold is, to go to
send({'jsonrpc': '2.0', 'method': 'textDocument/didChange',
      'params': {'textDocument': {'uri': uri(nest), 'version': 2},
                 'contentChanges': [{'text': '|%\n+$  foo  [a=@ud b=@t]\n++  bar\n  ^-  foo\n  [1 2]\n--\n'}]}})
send({'jsonrpc': '2.0', 'method': 'textDocument/didSave', 'params': {'textDocument': {'uri': uri(nest)}}})
d = waitdiagnostics(uri(nest))
rel = d and d[0].get('relatedInformation')
check('related information', rel and [(x['location']['uri'], x['location']['range']['start'], x['message']) for x in rel],
      [(uri(nest), {'line': 1, 'character': 0}, 'need: foo (+foo)')])
send({'jsonrpc': '2.0', 'method': 'textDocument/didClose', 'params': {'textDocument': {'uri': uri(nest)}}})
check('diagnostics cleared', waitdiagnostics(uri(nest)), [])

# highlighting doesn't wait for diagnostics: a file opened and its tokens
# asked for at once get the tokens first
opened = {'jsonrpc': '2.0', 'method': 'textDocument/didOpen',
          'params': {'textDocument': {'uri': uri(hoon), 'languageId': 'hoon', 'version': 1,
                                      'text': open(hoon).read()}}}
asked = {'jsonrpc': '2.0', 'id': 52, 'method': 'textDocument/semanticTokens/full',
         'params': {'textDocument': {'uri': uri(hoon)}}}
diagnostics.pop(uri(hoon), None)
proc.stdin.write(b''.join(b'Content-Length: %d\r\n\r\n' % len(b) + b
                          for b in (json.dumps(opened).encode(), json.dumps(asked).encode())))
proc.stdin.flush()
first = recv()
check('tokens before diagnostics', first.get('id'), 52)
check('then diagnostics', waitdiagnostics(uri(hoon)), [])
send({'jsonrpc': '2.0', 'method': 'textDocument/didClose', 'params': {'textDocument': {'uri': uri(hoon)}}})

# nothing at a position
check('no definition', definition(8, behn, 0, 0), None)

# unknown requests get an error, notifications are ignored
send({'jsonrpc': '2.0', 'method': 'workspace/didChangeConfiguration', 'params': {}})
r = request(9, 'textDocument/references', {})
check('unknown method', r.get('error', {}).get('code'), -32601)

r = request(10, 'shutdown', None)
check('shutdown', r.get('result', 'missing'), None)
send({'jsonrpc': '2.0', 'method': 'exit'})
check('exit code', proc.wait(timeout=10), 0)

print('%d/%d ok' % (total - bad, total))
