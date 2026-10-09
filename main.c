// hoon-lsp, a language server for hoon: highlighting, go to definition,
// hover and diagnostics, over the language server protocol on stdin and
// stdout. hoon.c does the parsing, typing and compiling, as for the
// compiler, hc/hc.c: files are built as the compiler builds them, the
// kernel as arvo does and desk files through ford as clay does, and the
// errors in them are the compiler's.

#include "hoon.c"

// Paths

char *pathcat(arena *a, char *x, char *y) {
  size n = 0, m = 0;
  while (x[n]) n++;
  while (y[m]) m++;
  char *r = new(a, char, n + m + 2);
  copy(r, x, n);
  r[n] = '/';
  copy(r + n + 1, y, m);
  return r;
}

b32 endswith(char *s, char *suffix) {
  size n = 0, m = 0;
  while (s[n]) n++;
  while (suffix[m]) m++;
  if (m > n) return 0;
  for (size i = 0; i < m; i++) {
    u8 c = (u8)s[n-m+i];
    if (c == '\\') c = '/';
    if (c != (u8)suffix[i]) return 0;
  }
  return 1;
}

char *oshome(void) {
  char *h = osgetenv("HOME");
  return h ? h : osgetenv("USERPROFILE");
}

size hairoffset(s8 src, hair h);
hair hairin(s8 src, size off);
b32 namechar(u8 c);

size parsesize(char *s) {
  size n = 0;
  for (; *s >= '0' && *s <= '9'; s++) n = n*10 + (*s - '0');
  return n;
}

// Documents open in an editor, which take the place of files on disk

typedef struct {
  char *path;
  u8   *buf;
  size  len;
  size  cap;
} document;

typedef struct {
  document *data;
  size      len;
  size      cap;
} documents;

documents *opendocs;

// a path with . and .. segments resolved and / separators
char *normpath(arena *a, char *path) {
  size n = 0;
  while (path[n]) n++;
  char *r = new(a, char, n + 2);
  size len = 0;
  b32 root = path[0] == '/' || path[0] == '\\';
  size i = 0;
  while (i <= n) {
    size s = i;
    while (i < n && path[i] != '/' && path[i] != '\\') i++;
    size m = i - s;
    char *seg = path + s;
    i++;
    if (m == 0 || (m == 1 && seg[0] == '.')) continue;
    if (m == 2 && seg[0] == '.' && seg[1] == '.') {
      // drop the last segment, unless there is none to drop
      size k = len;
      while (k > 0 && r[k-1] != '/') k--;
      b32 up = len - k == 2 && r[k] == '.' && r[k+1] == '.';
      if (len > 0 && !up) {
        len = k > 0 ? k - 1 : 0;
        continue;
      }
      if (root && len == 0) continue;
    }
    if (len > 0 || root) r[len++] = '/';
    copy(r + len, seg, m);
    len += m;
  }
  if (!len) r[len++] = root ? '/' : '.';
  r[len] = 0;
  return r;
}

document *finddoc(arena *a, char *path) {
  if (!opendocs) return 0;
  char *n = normpath(a, path);
  for (size i = 0; i < opendocs->len; i++) {
    if (streq(opendocs->data[i].path, n)) return &opendocs->data[i];
  }
  return 0;
}

// the text of a file, from the editor when it's open there
b32 readsource(arena *a, char *path, s8 *out) {
  document *d = finddoc(a, path);
  if (d) {
    out->buf = d->buf;
    out->len = d->len;
    return 1;
  }
  return osreadfile(a, path, out);
}

b32 fileexists(arena *a, char *path) {
  return finddoc(a, path) || osexists(path);
}

char *cstr(arena *a, u8 *buf, size len) {
  char *r = new(a, char, len + 1);
  copy(r, (byte*)buf, len);
  return r;
}

char *strjoin(arena *a, char *x, char *y) {
  size n = 0, m = 0;
  while (x[n]) n++;
  while (y[m]) m++;
  char *r = new(a, char, n + m + 1);
  copy(r, x, n);
  copy(r + n, y, m);
  return r;
}

// Where to look for what a desk doesn't have itself. A desk like
// landscape's is built against a ship's kernel, with libraries from
// desks like base-dev copied in, so these are set by the editor or on
// the command line.
typedef struct {
  char  *sys;     // a desk with sys/hoon.hoon, for files outside one
  char **deps;    // desks searched for imports and marks, in order
  size   ndeps;
} searchpath;

searchpath config;

// a configured path: ~/ is home, and a relative path is from root
char *configpath(arena *a, char *root, s8 p) {
  char *s = cstr(a, p.buf, p.len);
  if (p.len && s[0] == '~' && (p.len == 1 || s[1] == '/' || s[1] == '\\')) {
    char *home = oshome();
    if (home) s = pathcat(a, home, p.len > 2 ? s + 2 : ".");
  } else if (root && p.len && s[0] != '/' && s[0] != '\\' && !(p.len > 1 && s[1] == ':')) {
    s = pathcat(a, root, s);
  }
  return normpath(a, s);
}

// the desk for a configured sys: the directory itself if it has
// sys/hoon.hoon, or its parent if it is the sys directory
char *sysdesk(arena *a, char *dir) {
  if (!fileexists(a, pathcat(a, dir, "sys/hoon.hoon")) && fileexists(a, pathcat(a, dir, "hoon.hoon"))) {
    return normpath(a, pathcat(a, dir, ".."));
  }
  return dir;
}

void setsys(arena *a, char *root, s8 p) {
  config.sys = p.len ? sysdesk(a, configpath(a, root, p)) : 0;
}

void adddep(arena *a, char *root, s8 p) {
  char **deps = new(a, char *, config.ndeps + 1);
  for (size i = 0; i < config.ndeps; i++) deps[i] = config.deps[i];
  deps[config.ndeps++] = configpath(a, root, p);
  config.deps = deps;
}

// the file at rel in the desk, or else in the first desk searched that
// has it, or else where it would be in the desk
char *findfile(arena *a, char *desk, char *rel) {
  char *path = pathcat(a, desk, rel);
  if (fileexists(a, path)) return path;
  for (size i = 0; i < config.ndeps; i++) {
    char *dep = pathcat(a, config.deps[i], rel);
    if (fileexists(a, dep)) return dep;
  }
  return path;
}

// an import in a ford header
enum { dep_hoon, dep_arch, dep_noun };

typedef struct {
  i32   kind;
  u8    rune;   // for marks: % $ or *
  noun mark;   // the mark of /% and /*, the source mark of /$
  noun mark2;  // the target mark of /$
  noun name;   // the imported name, for /- and /+
  noun face;   // the face it binds, or 0
  char *path;   // the file imported, or directory for /~
  noun spec;   // the spec of /~
  size  at;     // offsets of the imported name in the header
  size  end;
  size  faceat; // offset of the face in the header
  size  jumps[3][2];  // other ranges in the header: marks and a path
  char *targets[3];   // and the files they name
} import;

typedef struct {
  import *data;
  size    len;
  size    cap;
} imports;

// the file for an import of name under a prefix, /sur or /lib, as ford
// finds it: in the desk, or the desks searched; 0 if there's none
char *importfile(arena *a, char *desk, char *prefix, noun name) {
  fordenv f = {0};
  f.desk = desk;
  f.deps = config.deps;
  f.ndeps = (i32)config.ndeps;
  parser p = newparser(a, (s8){0, 0});
  noun pax = fitpath(&p, &f, prefix, name);
  return pax ? deskfile(a, &f, pax) : 0;
}

// a path like /foo/bar, as a string
char *stapstr(parser *p, size *pos) {
  size s = *pos;
  if (!chr(p, pos, '/')) return 0;
  for (;;) {
    urs(p, pos);
    size t = *pos;
    if (!chr(p, pos, '/')) {
      *pos = t;
      break;
    }
  }
  note(p, s, *pos, tok_string);
  return cstr(p->a, (u8*)p->buf + s, *pos - s);
}

// an import in /- or /+: name, *name or face=name
b32 taut(parser *p, size *pos, import *d) {
  size s = *pos;
  noun a, b;
  if (chr(p, pos, '*') && (b = sym(p, pos))) {
    d->face = 0;
    d->at = s + 1;
    d->end = *pos;
    d->name = b;
    return 1;
  }
  *pos = s;
  if (!(a = sym(p, pos))) return 0;
  size t = *pos;
  if (chr(p, pos, '=') && (b = sym(p, pos))) {
    d->face = a;
    d->faceat = s;
    d->at = t + 1;
    d->end = *pos;
    d->name = b;
    return 1;
  }
  *pos = t;
  d->face = a;
  d->faceat = s;
  d->at = s;
  d->end = t;
  d->name = a;
  return 1;
}
// parse the ford header of a desk file, as +pile-rule, leaving pos at
// the start of the body
b32 fordheader(parser *p, size *pos, char *desk, imports *deps) {
  arena *a = p->a;
  gay(p, pos);
  size s = *pos;
  if (!(jest(p, pos, "/?") && gap(p, pos) && dem(p, pos) && gap(p, pos))) *pos = s;
  for (;;) {
    s = *pos;
    if (!chr(p, pos, '/') || *pos >= p->len) {
      *pos = s;
      return 1;
    }
    u8 c = p->buf[*pos];
    if (c != '-' && c != '+' && c != '=' && c != '~' && c != '%' && c != '$' && c != '*') {
      *pos = s;
      return 1;
    }
    chr(p, pos, c);
    if (!gap(p, pos)) return 0;
    note(p, s, s + 2, tok_keyword);
    if (c == '-' || c == '+') {
      for (;;) {
        import d = {0};
        if (!taut(p, pos, &d)) return 0;
        note(p, d.at, d.end, tok_variable);
        if (d.face && d.faceat != d.at) note(p, d.faceat, d.faceat + alen(d.face), tok_variable);
        d.kind = dep_hoon;
        d.path = importfile(a, desk, c == '-' ? "sur" : "lib", d.name);
        *push(deps, a) = d;
        size t = *pos;
        if (chr(p, pos, ',') && gaw(p, pos)) continue;
        *pos = t;
        break;
      }
    } else {
      import d = {0};
      size t = *pos;
      if (!(d.face = sym(p, pos)) || !gap(p, pos)) return 0;
      note(p, t, t + alen(d.face), tok_variable);
      d.faceat = t;
      d.at = t;
      d.end = t + alen(d.face);
      if (c == '=') {
        char *path = stapstr(p, pos);
        if (!path) return 0;
        d.kind = dep_hoon;
        d.path = findfile(a, desk, strjoin(a, path + 1, ".hoon"));
      } else if (c == '~') {
        if (!(d.spec = wyde(p, pos)) || !gap(p, pos)) return 0;
        char *path = stapstr(p, pos);
        if (!path) return 0;
        d.kind = dep_arch;
        d.path = pathcat(a, desk, path + 1);
      } else {
        // marks and conversions: /% face %mark, /$ face %a %b, /* face %mark /path
        noun mark;
        if (!chr(p, pos, '%')) return 0;
        size m = *pos;
        if (!(mark = sym(p, pos))) return 0;
        d.jumps[0][0] = m;
        d.jumps[0][1] = *pos;
        note(p, m - 1, *pos, tok_term);
        d.targets[0] = importfile(a, desk, "mar", mark);
        if (c == '$') {
          if (!gap(p, pos) || !chr(p, pos, '%')) return 0;
          m = *pos;
          if (!(d.mark2 = sym(p, pos))) return 0;
          d.jumps[1][0] = m;
          d.jumps[1][1] = *pos;
          note(p, m - 1, *pos, tok_term);
          d.targets[1] = importfile(a, desk, "mar", d.mark2);
        }
        if (c == '*') {
          if (!gap(p, pos)) return 0;
          m = *pos;
          char *path = stapstr(p, pos);
          if (!path) return 0;
          // the last segment is the extension: /app/foo/hoon is app/foo.hoon
          size n = 0, k = 0;
          while (path[n]) n++;
          for (k = n; k > 1 && path[k-1] != '/'; k--) {}
          if (k > 1) {
            char *file = cstr(a, (u8*)path + 1, n - 1);
            file[k-2] = '.';
            d.jumps[2][0] = m;
            d.jumps[2][1] = *pos;
            d.targets[2] = findfile(a, desk, file);
          }
        }
        d.kind = dep_noun;
        d.rune = c;
        d.mark = mark;
        d.path = importfile(a, desk, "mar", mark);
      }
      *push(deps, a) = d;
    }
    if (!gap(p, pos)) return 0;
  }
}
// a file in the kernel, typed by its place in the kernel chain
b32 kernelfile(char *path) {
  for (size i = 0; path[i]; i++) {
    if (path[i] == '/' && path[i+1] == 's' && path[i+2] == 'y' && path[i+3] == 's'
        && path[i+4] == '/') {
      return 1;
    }
  }
  return 0;
}
b32 linestart(s8 src, size i) {
  return i == 0 || src.buf[i-1] == '\n';
}
// blank a range, keeping newlines and so all positions
void blank(s8 src, size from, size to) {
  for (size i = from; i < to && i < src.len; i++) {
    if (src.buf[i] != '\n') src.buf[i] = ' ';
  }
}
// replace the body of the arm around an offset with !!
b32 stubarm(s8 src, size at) {
  // the arm: the last line starting with ++ or +$ at or before the error
  size start = at < src.len ? at : src.len;
  size indent = 0;
  for (;;) {
    while (start > 0 && !linestart(src, start)) start--;
    size i = start;
    while (i < src.len && src.buf[i] == ' ') i++;
    if (i + 1 < src.len && src.buf[i] == '+' && (src.buf[i+1] == '+' || src.buf[i+1] == '$')) {
      indent = i - start;
      start = i;
      break;
    }
    if (start == 0) return 0;
    start--;
  }
  // its body ends at the next arm, chapter or -- at the same indentation
  size end = start + 2;
  for (;;) {
    while (end < src.len && src.buf[end] != '\n') end++;
    if (end >= src.len) break;
    size i = end + 1;
    while (i < src.len && src.buf[i] == ' ') i++;
    if (i - end - 1 <= indent && i + 1 < src.len
        && ((src.buf[i] == '+' && (src.buf[i+1] == '+' || src.buf[i+1] == '$' || src.buf[i+1] == '|'))
            || (src.buf[i] == '-' && src.buf[i+1] == '-'))) {
      end = end + 1;
      break;
    }
    end++;
  }
  // keep "++  name", then !! in place of the body
  size i = start + 2;
  while (i < end && src.buf[i] == ' ') i++;
  while (i < end && namechar(src.buf[i])) i++;
  if (i + 3 > end) return 0;
  blank(src, i, end);
  src.buf[i+2] = '!';
  src.buf[i+3] = '!';
  return 1;
}
// the directory holding sys/hoon.hoon for a file: above it, or else the
// one configured
char *deskof(arena *a, char *path) {
  size n = 0;
  while (path[n]) n++;
  char *dir = new(a, char, n + 3);
  copy(dir, path, n);
  // strip the file name
  size k = n;
  while (k > 0 && dir[k-1] != '/' && dir[k-1] != '\\') k--;
  if (k == 0) {
    dir[0] = '.';
    dir[1] = 0;
  } else {
    dir[k-1] = 0;
  }
  for (i32 up = 0; up < 32; up++) {
    char *cands[3] = {"sys/hoon.hoon", "arvo/sys/hoon.hoon", "base/sys/hoon.hoon"};
    char *roots[3] = {dir, pathcat(a, dir, "arvo"), pathcat(a, dir, "base")};
    for (i32 i = 0; i < 3; i++) {
      if (osexists(pathcat(a, dir, cands[i]))) return roots[i];
    }
    dir = pathcat(a, dir, "..");
  }
  return config.sys;
}
// the root of the desk a file is in: the nearest directory with a
// sys.kelvin or desk.bill
char *deskroot(arena *a, char *path) {
  size n = 0;
  while (path[n]) n++;
  size k = n;
  while (k > 0 && path[k-1] != '/' && path[k-1] != '\\') k--;
  char *dir = k ? cstr(a, (u8*)path, k - 1) : ".";
  for (i32 up = 0; up < 32; up++) {
    if (fileexists(a, pathcat(a, dir, "sys.kelvin")) || fileexists(a, pathcat(a, dir, "desk.bill"))) {
      return dir;
    }
    dir = pathcat(a, dir, "..");
  }
  return 0;
}
// The kernel and desks. The kernel is built as arvo builds it, from the
// sys directory of a desk, or read from the compiler's cache, and kept
// with the subject each of its files is compiled against. The imports of
// desk files are built through ford as clay builds them, and kept until
// a file they were built from changes. The heap is only ever added to,
// and collected now and then, with these as what's kept.

typedef struct {
  char *sys;        // the sys directory, or 0 before there's a kernel
  noun  subs[6];    // %noun, and after hoon, arvo, ..part, lull and zuse
} kernelctx;

kernelctx kern;

enum { maxdesks = 64 };
fordenv *desks[maxdesks];
i32      ndesks;

// the kernel from a sys directory, made or kept; 0 if it can't be built,
// with why on err. It must come before anything else is made in the heap
// for the cache to be read, so every request asks for it first
b32 kernelfor(arena *a, char *sys, bufout *err) {
  if (kern.sys && streq(kern.sys, sys)) return 1;
  // another kernel: what was built against the last goes
  ndesks = 0;
  kern.sys = 0;
  char *subjects[] = {pathcat(a, sys, "hoon.hoon"), pathcat(a, sys, "arvo.hoon"), "..part",
                      pathcat(a, sys, "lull.hoon"), pathcat(a, sys, "zuse.hoon")};
  i32 exprs[] = {2, 2, 1, 2, 2};
  b32 compact;
  if (!chainmake(a, err, subjects[0], subjects, exprs, countof(subjects), kern.subs, &compact)) {
    return 0;
  }
  size n = (size)__builtin_strlen(sys);
  kern.sys = new(&H.perm, char, n + 1);
  copy(kern.sys, sys, n + 1);
  return 1;
}

// the subject a file of the kernel is compiled against, by its name
noun kernelsubject(char *path) {
  if (endswith(path, "/sys/hoon.hoon")) return kern.subs[0];
  if (endswith(path, "/sys/arvo.hoon")) return kern.subs[1];
  if (endswith(path, "/sys/lull.hoon")) return kern.subs[3];
  if (endswith(path, "/sys/zuse.hoon")) return kern.subs[4];
  return kern.subs[5];
}

// the builds of the desk at root, made if there are none
fordenv *deskfor(char *root) {
  for (i32 i = 0; i < ndesks; i++) {
    if (streq(desks[i]->desk, root)) return desks[i];
  }
  if (ndesks == maxdesks) {
    // the oldest goes
    for (i32 i = 1; i < ndesks; i++) desks[i-1] = desks[i];
    ndesks--;
  }
  fordenv *f = new(&H.perm, fordenv, 1);
  size n = (size)__builtin_strlen(root);
  f->desk = new(&H.perm, char, n + 1);
  copy(f->desk, root, n + 1);
  f->deps = config.deps;
  f->ndeps = (i32)config.ndeps;
  f->zuse = kern.subs[5];
  desks[ndesks++] = f;
  return f;
}

// whether path is under dir, and what's after dir/ if it is
char *under(char *path, char *dir) {
  size k = 0;
  while (dir[k] && dir[k] == path[k]) k++;
  if (dir[k] || path[k] != '/') return 0;
  return path + k + 1;
}

// a file has changed: forget what was built from it
void forgetfile(arena *a, char *path) {
  for (i32 i = 0; i < ndesks; i++) {
    fordenv *f = desks[i];
    char *rel = under(path, f->desk);
    for (i32 k = 0; !rel && k < f->ndeps; k++) rel = under(path, f->deps[k]);
    if (rel) fordforget(f, deskpath(a, rel, (size)__builtin_strlen(rel)));
  }
}

// Source files, as the language server reads them: with spots under !.
// too, and a desk file's header read twice, as clay reads it and for
// where its imports are

typedef struct {
  char   *path;
  s8      src;      // its text, from the editor if it's open there
  noun    wer;      // the path in its spots: in the desk, or in sys
  b32     kernel;   // a file of the kernel, compiled as arvo does
  char   *desk;     // the top of its desk, if it's in one
  i32     file;     // in srcfiles
  noun    gen;      // its hoon, the body of a desk file
  noun    pil;      // a desk file's header and body, as +pile-rule
  imports deps;     // the imports in its header
} lspfile;

// parse a source file; on failure, the offset of the error. budget is
// what's left of the file's, or -1 to start one
noun parsesrc(arena *a, lspfile *f, s8 src, size *err, spans *toks, size *budget) {
  parser *q = new(a, parser, 1);
  *q = newparser(a, src);
  if (*budget >= 0) q->budget = *budget;
  q->toks = toks;
  q->bug = 1;
  q->allbug = 1;
  q->file = f->file;
  size pos = 0;
  noun gen = 0;
  f->pil = 0;
  if (!f->desk) {
    q->wer = f->wer;
    gen = vest(q, &pos);
  } else {
    // the header for where its imports are, highlighted
    parser h = *q;
    h.budget = q->budget;
    f->deps.len = 0;
    fordheader(&h, &pos, f->desk, &f->deps);
    // and the whole as clay reads it
    f->pil = pilerule(q, f->wer);
    gen = f->pil ? tl(tl(tl(tl(tl(tl(tl(f->pil))))))) : 0;
    if (!gen) f->pil = 0;
  }
  if (!gen) *err = hairoffset(src, errhair(q));
  *budget = q->budget;
  return gen;
}

// parse a file, recovering from syntax errors on a copy: blank the line
// of the error, or failing that, stub out the arm around it. The tries
// share one budget, or they could take 32 times as long; what a failed
// one makes is mostly the same nouns as the next, and the rest is
// garbage that's collected
noun parsefile(arena *a, lspfile *f, spans *toks) {
  size err;
  size budget = -1;
  noun gen = parsesrc(a, f, f->src, &err, toks, &budget);
  s8 fix = {new(a, u8, f->src.len + 1), f->src.len};
  copy((byte*)fix.buf, (byte*)f->src.buf, f->src.len);
  for (i32 tries = 0; !gen && tries < 32; tries++) {
    s8 line = {new(a, u8, fix.len + 1), fix.len};
    copy((byte*)line.buf, (byte*)fix.buf, fix.len);
    size from = err < line.len ? err : line.len;
    while (from > 0 && !linestart(line, from)) from--;
    size to = err;
    while (to < line.len && line.buf[to] != '\n') to++;
    blank(line, from, to);
    size err2;
    if (toks) toks->len = 0;
    if ((gen = parsesrc(a, f, line, &err2, toks, &budget))) break;
    if (err2 > err) {
      fix = line;
      err = err2;
      continue;
    }
    if (!stubarm(fix, err)) break;
    if (toks) toks->len = 0;
    gen = parsesrc(a, f, fix, &err, toks, &budget);
  }
  return gen;
}

// the sys directory for a file: in a desk above it, or else configured
char *sysfor(arena *a, char *path) {
  char *desk = deskof(a, path);
  return desk ? osabspath(a, pathcat(a, desk, "sys")) : 0;
}

// a file as the language server sees it, not yet parsed: where it is,
// and the path in its spots; 0 if it can't be read
b32 openfile(arena *a, char *path, lspfile *f) {
  *f = (lspfile){0};
  // a relative path, as on the command line, from where we are; links in
  // an absolute one stay, as an editor has them
  if (path[0] != '/' && path[0] != '\\' && !(path[0] && path[1] == ':')) {
    char *cwd = osabspath(a, ".");
    if (cwd) path = pathcat(a, cwd, path);
  }
  f->path = normpath(a, path);
  if (!readsource(a, f->path, &f->src)) return 0;
  f->kernel = kernelfile(f->path);
  f->desk = f->kernel ? 0 : deskroot(a, f->path);
  if (f->desk) f->desk = normpath(a, f->desk);
  if (f->kernel) {
    f->wer = syspath(a, f->path);
  } else if (f->desk) {
    char *rel = under(f->path, f->desk);
    f->wer = rel ? deskpath(a, rel, (size)__builtin_strlen(rel)) : 0;
    if (!rel) f->desk = 0;
  }
  if (!f->wer) f->wer = cons(a, atomcstr(a, f->path), nul);
  // loose, for highlighting, it's not kept
  f->file = loose.on ? 0 : srcadd(f->path, f->wer, f->src);
  return 1;
}

// the subject a file is compiled against: as arvo compiles the kernel,
// or with its imports as clay builds them, whose faces carry the spots
// of their names in the header; 0 if the imports can't be built, with
// why on err
noun filesubject(arena *a, lspfile *f, bufout *err) {
  if (f->kernel) return kernelsubject(f->path);
  if (!f->desk || !f->pil) return kern.subs[5];
  fordenv *d = deskfor(f->desk);
  parser q = newparser(a, f->src);
  q.file = f->file;
  q.wer = f->wer;
  parser *p = &q;
  noun sut = fordsubject(p, d, f->pil, err);
  if (!sut) return 0;
  // the imports from the outside in, matched to the header's
  size n = f->deps.len;
  noun *heads = new(a, noun, n + 1);
  noun rest = sut;
  size k = 0;
  for (; k < n && tagis(rest, "cell"); k++) {
    heads[k] = hd(tl(rest));
    rest = tl(tl(rest));
  }
  if (k != n || rest != d->zuse) return sut;
  for (size i = 0; i < n; i++) {
    import *dep = &f->deps.data[i];
    noun h = heads[n - 1 - i];
    if (dep->face && tagis(h, "face") && hd(tl(h)) == dep->face) {
      hair s = hairin(f->src, dep->faceat), e = hairin(f->src, dep->faceat + alen(dep->face));
      noun spot = C2(f->wer, C2(C2(D((u64)s.line), D((u64)s.col)), C2(D((u64)e.line), D((u64)e.col))));
      h = C3(K("hint"), C2(K("noun"), C2(K("spot"), spot)), h);
    }
    rest = C3(K("cell"), h, rest);
  }
  return rest;
}

// Go to definition. Parse the file with %dbug spots, find the wing under
// the cursor, compute the subject type at that point by threading types
// through the hoon the way +mint does, and resolve the wing against it
// with +fond, playing with spots so that faces say where they were made.
// Arms are found by their spots, or where the parser saw them; faces by
// the spot of what made them, or in the arm of the mold that made them.

typedef struct {
  typer   *u;
  noun     wer;    // the file the cursor is in, by the path in its spots
  hair     at;     // the cursor
  noun     best;   // innermost spot containing the cursor
  noun     full;   // wing under the cursor
  size     limb;   // index of the cursor limb in it
  noun     spot;   // innermost spot around the walk
  i32      inbest; // whether best encloses the walk; spec expansion can
                   // nest a larger spot inside it
  noun     sut;    // subject where the wing was found
  noun     want;   // the wing to resolve there, from the cursor limb on
  b32      done;
} walker;

// where a spot [wer [line col] [line col]] starts and ends
hair spotfrom(noun spot) {
  noun s = hd(tl(spot));
  return (hair){(size)atomlow(hd(s)), (size)atomlow(tl(s))};
}

hair spotto(noun spot) {
  noun e = tl(tl(spot));
  return (hair){(size)atomlow(hd(e)), (size)atomlow(tl(e))};
}

b32 hairbefore(hair x, hair y) {
  return x.line < y.line || (x.line == y.line && x.col < y.col);
}

b32 spotcontains(noun spot, noun wer, hair at) {
  return hd(spot) == wer && !hairbefore(at, spotfrom(spot)) && hairbefore(at, spotto(spot));
}

b32 isdbug(noun gen) {
  return iscell(gen) && tagis(gen, "dbug") && iscell(tl(gen)) && iscell(hd(tl(gen)));
}

// the innermost spot containing a position, syntactically
void innermost(noun gen, noun wer, hair at, noun *best) {
  while (iscell(gen)) {
    if (isdbug(gen) && spotcontains(hd(tl(gen)), wer, at)) *best = hd(tl(gen));
    innermost(hd(gen), wer, at, best);
    gen = tl(gen);
  }
}

b32 gwalk(walker *w, noun sut, noun gen);

b32 gwalkall(walker *w, noun sut, noun list) {
  for (; iscell(list); list = tl(list)) {
    if (gwalk(w, sut, hd(list))) return 1;
  }
  return 0;
}

// whether a wing in the hoon is the one under the cursor; desugaring
// may add limbs at the end
b32 gmatch(walker *w, noun wing) {
  for (noun f = w->full; iscell(f); f = tl(f), wing = tl(wing)) {
    if (isatom(wing) || !nouneq(hd(f), hd(wing))) return 0;
  }
  return 1;
}

b32 gfound(walker *w, noun sut, noun wing) {
  w->sut = sut;
  w->want = slag(w->limb, wing);
  w->done = 1;
  return 1;
}

noun firstspot(noun gen);

// walk the arms of a battery against the core type, skipping arms whose
// body is elsewhere
b32 gwalkarms(walker *w, noun cor, noun dom) {
  if (isatom(dom)) return 0;
  noun tome = tl(mapn(dom));
  nouns arms = {0};
  maptap(tome, &arms, w->u->p->a);
  for (size i = 0; i < arms.len; i++) {
    noun arm = tl(arms.data[i]);
    noun body = arm;
    b32 inside = 0;
    // +* aliases wrap every arm of a door; they count too
    while (tagis(body, "tstr")) {
      noun spot = firstspot(hd(tl(tl(body))));
      if (spot && spotcontains(spot, w->wer, w->at)) inside = 1;
      body = tl(tl(tl(body)));
    }
    noun spot = firstspot(body);
    if (!inside && spot && !spotcontains(spot, w->wer, w->at)) continue;
    if (gwalk(w, cor, arm)) return 1;
  }
  return gwalkarms(w, cor, mapl(dom)) || gwalkarms(w, cor, mapr(dom));
}

// true when the search is over: found, or failed for good
b32 gwalk(walker *w, noun sut, noun gen) {
  typer *u = w->u;
  parser *p = u->p;
  for (;;) {
    if (w->done) return 1;
    noun g = tl(gen);
    if (iscell(hd(gen))) {
      // the tail in the loop, as a tuple can be as long as the file
      if (gwalk(w, sut, hd(gen))) return 1;
      gen = g;
      continue;
    }
    noun tag = hd(gen);
    #define IS(t) atomeqc(tag, t)
    if (IS("dbug")) {
      noun spot = hd(g);
      if (!spotcontains(spot, w->wer, w->at)) return 0;
      noun outer = w->spot;
      noun played = u->spot;
      w->spot = u->spot = spot;
      w->inbest += spot == w->best;
      b32 r = gwalk(w, sut, tl(g));
      w->inbest -= spot == w->best;
      w->spot = outer;
      u->spot = played;
      return r;
    }
    b32 here = w->inbest > 0;
    if (IS("cnts")) {
      if (here && gmatch(w, hd(g))) return gfound(w, sut, hd(g));
      for (noun l = tl(g); iscell(l); l = tl(l)) {
        if (here && gmatch(w, hd(hd(l)))) {
          // changes apply to the target leg, or to the core of an arm
          noun lug = find(u, sut, "read", hd(g));
          if (!lug) return w->done = 1;
          noun t = tl(lug);
          if (atomis(hd(lug), 0)) {
            noun opal = tl(tl(lug));
            t = atomis(hd(opal), 0) ? tl(opal) : tforkheads(u, tl(tl(opal)));
          }
          return gfound(w, t, hd(hd(l)));
        }
        if (gwalk(w, sut, tl(hd(l)))) return 1;
      }
      return 0;
    }
    if (IS("fits") || IS("wthx")) {
      if (here && gmatch(w, tl(g))) return gfound(w, sut, tl(g));
      if (IS("fits")) {
        gen = hd(g);
        continue;
      }
      return 0;
    }
    if (IS("brcn") || IS("brpt")) {
      noun cor = play(u, sut, gen);
      if (!cor) return 0;
      return gwalkarms(w, cor, tl(g));
    }
    if (IS("tsgr")) {
      if (gwalk(w, sut, hd(g))) return 1;
      noun t = play(u, sut, hd(g));
      if (!t) return 0;
      sut = t;
      gen = tl(g);
      continue;
    }
    if (IS("tscm")) {
      if (gwalk(w, sut, hd(g))) return 1;
      sut = C3(K("face"), C2(nul, C2(hd(g), nul)), sut);
      gen = tl(g);
      continue;
    }
    if (IS("wtcl")) {
      if (gwalk(w, sut, hd(g))) return 1;
      // a branch that can't be taken is dead code to the compiler, but
      // its names still mean something; use the unnarrowed subject
      noun fex = chip(u, sut, 1, hd(g));
      noun wux = chip(u, sut, 0, hd(g));
      if (!fex || isvoid(fex)) fex = sut;
      if (!wux || isvoid(wux)) wux = sut;
      if (gwalk(w, fex, hd(tl(g)))) return 1;
      return gwalk(w, wux, tl(tl(g)));
    }
    if (IS("dtkt")) {
      return gwalk(w, sut, C2(K("kttr"), hd(g))) || gwalk(w, sut, tl(g));
    }
    if (IS("dtls") || IS("dtwt") || IS("ktbr") || IS("ktpm") || IS("ktwt") || IS("ktsg")
        || IS("zpts") || IS("lost")) {
      gen = g;
      continue;
    }
    if (IS("dttr") || IS("dtts") || IS("ktls") || IS("ktcb") || IS("zpcm") || IS("zpmc")) {
      if (gwalk(w, sut, hd(g))) return 1;
      gen = tl(g);
      continue;
    }
    if (IS("note")) {
      gen = tl(g);
      continue;
    }
    if (IS("sggr")) {
      noun hint = hd(g);
      if (iscell(hint) && gwalk(w, sut, tl(hint))) return 1;
      gen = tl(g);
      continue;
    }
    if (IS("sgzp")) {
      if (gwalk(w, sut, hd(g))) return 1;
      gen = tl(g);
      continue;
    }
    if (IS("zpgl")) {
      return gwalk(w, sut, C2(K("kttr"), hd(g))) || gwalk(w, sut, tl(g));
    }
    if (IS("zppt")) {
      return gwalk(w, sut, hd(tl(g))) || gwalk(w, sut, tl(tl(g)));
    }
    if (IS("tune")) {
      // aliases and bridges of =* and =,
      if (isatom(g)) return 0;
      nouns xs = {0};
      maptap(hd(g), &xs, p->a);
      for (size i = 0; i < xs.len; i++) {
        noun v = tl(xs.data[i]);
        if (iscell(v) && gwalk(w, sut, tl(v))) return 1;
      }
      return gwalkall(w, sut, tl(g));
    }
    if (IS("rock") || IS("sand") || IS("hand") || IS("zpzp")) return 0;
    #undef IS
    noun doz = hoonopen(p, gen);
    if (!doz || nouneq(doz, gen)) return 0;
    gen = doz;
  }
}

// parse a wing at s, and find the limb covering the cursor
noun wingfrom(parser *p, size s, size cur, size *which) {
  nouns limbs = {0};
  size pos = s;
  size k = (size)-1;
  for (;;) {
    size start = pos;
    noun l = limb(p, &pos);
    if (!l) break;
    if (start <= cur && cur < pos) k = limbs.len;
    *push(&limbs, p->a) = l;
    size t = pos;
    if (!chr(p, &pos, '.')) break;
    if (t <= cur && cur < pos && k == (size)-1) k = limbs.len;
  }
  if (!limbs.len || k == (size)-1 || k >= limbs.len) return 0;
  *which = k;
  return nounslist(p, &limbs);
}

// the wing under the cursor, and the index of the limb under it; the
// innermost span around the cursor usually starts at the wing
noun wingat(parser *p, size cur, size spanstart, size *which) {
  if (spanstart <= cur) {
    noun w = wingfrom(p, spanstart, cur, which);
    if (w) return w;
  }
  size s = cur;
  #define WINGCHAR(c) (((c) >= 'a' && (c) <= 'z') || ((c) >= '0' && (c) <= '9') || (c) == '-' \
    || (c) == '.' || (c) == '+' || (c) == '<' || (c) == '>' || (c) == '&' || (c) == '|' \
    || (c) == '^' || (c) == '$' || (c) == ',')
  if (cur >= p->len || !WINGCHAR(p->buf[cur])) return 0;
  while (s > 0 && WINGCHAR(p->buf[s-1])) s--;
  #undef WINGCHAR
  // parse limb by limb from each possible start, keeping the one that
  // covers the cursor
  for (; s <= cur; s++) {
    noun w = wingfrom(p, s, cur, which);
    if (w) return w;
  }
  return 0;
}

// the first spot in a hoon
noun firstspot(noun gen) {
  while (iscell(gen)) {
    if (isdbug(gen)) return hd(tl(gen));
    noun s = firstspot(hd(gen));
    if (s) return s;
    gen = tl(gen);
  }
  return 0;
}

size hairoffset(s8 src, hair h) {
  size line = 1, i = 0;
  for (; i < src.len && line < h.line; i++) {
    if (src.buf[i] == '\n') line++;
  }
  return i + h.col - 1;
}

hair hairin(s8 src, size off) {
  hair h = {1, 1};
  for (size i = 0; i < off && i < src.len; i++) {
    if (src.buf[i] == '\n') {
      h.line++;
      h.col = 1;
    } else {
      h.col++;
    }
  }
  return h;
}

b32 namechar(u8 c) {
  return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-';
}

b32 nameat(s8 src, size i, noun name) {
  if (i + alen(name) > src.len) return 0;
  for (size j = 0; j < alen(name); j++) {
    if (src.buf[i+j] != abyte(name, j)) return 0;
  }
  if (i > 0 && namechar(src.buf[i-1])) return 0;
  if (i + alen(name) < src.len && namechar(src.buf[i+alen(name)])) return 0;
  return 1;
}

// the first occurrence of a name between offsets, or the start
size nameinspan(s8 src, size i, size end, noun name) {
  if (end > src.len) end = src.len;
  if (!isatom(name) || !alen(name)) return i;
  for (size j = i; j < end; j++) {
    if (nameat(src, j, name)) return j;
  }
  // a face named by =prefix=spec is prefix-name
  for (size k = 1; k < alen(name); k++) {
    if (abyte(name, k) != '-') continue;
    for (size j = i; j + k + 1 < end; j++) {
      if (src.buf[j] != '=' || src.buf[j+k+1] != '=') continue;
      b32 match = 1;
      for (size m = 0; m < k; m++) match &= src.buf[j+1+m] == abyte(name, m);
      // the rest of the name starts the spec, after any ^ or _
      size r = j + k + 2;
      while (r < end && !namechar(src.buf[r])) r++;
      for (size m = k + 1; m < alen(name) && match; m++, r++) {
        match = r < end && src.buf[r] == abyte(name, m);
      }
      if (match) return j + 1;
    }
  }
  return i;
}

// an arm name before its body: "++  name" or "+$  name"
size armname(s8 src, size start, noun name) {
  if (!alen(name)) return start;
  size i = start;
  for (size k = 0; k < 4096 && i > 0; k++) {
    i--;
    if (nameat(src, i, name)) {
      size j = i;
      while (j > 0 && (src.buf[j-1] == ' ' || src.buf[j-1] == '\n')) j--;
      if (j >= 2 && src.buf[j-2] == '+' && (src.buf[j-1] == '+' || src.buf[j-1] == '$')) {
        return i;
      }
    }
  }
  return start;
}

// the end of the arm whose name is at an offset: the next line that
// starts an arm or ends the core no deeper than it
size armend(s8 src, size at) {
  size s = at;
  while (s > 0 && src.buf[s-1] != '\n') s--;
  size ind = 0;
  while (s + ind < src.len && src.buf[s+ind] == ' ') ind++;
  size e = at;
  for (;;) {
    while (e < src.len && src.buf[e] != '\n') e++;
    if (e >= src.len) return src.len;
    size i = ++e, k = 0;
    while (i < src.len && src.buf[i] == ' ') i++, k++;
    if (k <= ind && i + 1 < src.len
        && ((src.buf[i] == '+' && (src.buf[i+1] == '+' || src.buf[i+1] == '$' || src.buf[i+1] == '|'))
            || (src.buf[i] == '-' && src.buf[i+1] == '-'))) {
      return e;
    }
  }
}

typedef struct {
  char *path;
  s8    src;
  size  at;     // a byte offset in it
  b32   ok;
} location;

// a file in srcfiles, with its text as it is now
b32 srcloc(arena *a, i32 file, location *loc) {
  if (file <= 0 || file >= srcfiles.len) return 0;
  loc->path = srcfiles.data[file].name;
  return readsource(a, loc->path, &loc->src);
}

// the name of an arm named name whose ++ or +$ is at an offset, or -1
size armnamed(s8 src, size at, noun name) {
  if (at + 2 > src.len || src.buf[at] != '+' || (src.buf[at+1] != '+' && src.buf[at+1] != '$')) {
    return (size)-1;
  }
  size i = at + 2;
  while (i < src.len && (src.buf[i] == ' ' || src.buf[i] == '\n')) i++;
  return i < src.len && nameat(src, i, name) ? i : (size)-1;
}

// where an arm is: by the spots in its body, or where the parser saw it,
// in a file without spots
b32 armloc(arena *a, noun arm, noun name, location *loc) {
  // +* aliases wrap every arm of a door
  while (tagis(arm, "tstr")) arm = tl(tl(tl(arm)));
  noun spot = firstspot(arm);
  if (spot) {
    if (!srcloc(a, srcbywer(hd(spot)), loc)) return 0;
    loc->at = armname(loc->src, hairoffset(loc->src, spotfrom(spot)), name);
    return loc->ok = 1;
  }
  noun v = nounmapget(&armsites, armkey(a, arm));
  if (!v) return 0;
  u64 x = atomlow(v);
  if (!srcloc(a, (i32)(x >> 40), loc)) return 0;
  hair h = {(size)(x >> 16 & 0xffffff), (size)(x & 0xffff)};
  size at = hairoffset(loc->src, h);
  // after the ++ or +$, at the name; or if the file has changed since,
  // as it may in the editor, the arm of that name nearest where it was
  size i = armnamed(loc->src, at, name);
  if (i == (size)-1) {
    size best = (size)-1, dist = 0;
    for (size j = 0; j < loc->src.len; j++) {
      if (j && loc->src.buf[j-1] != '\n') continue;
      size k = j;
      while (k < loc->src.len && loc->src.buf[k] == ' ') k++;
      size n = armnamed(loc->src, k, name);
      if (n == (size)-1) continue;
      size d = k > at ? k - at : at - k;
      if (best == (size)-1 || d < dist) {
        best = n;
        dist = d;
      }
    }
    i = best;
  }
  loc->at = i == (size)-1 ? at : i;
  return loc->ok = 1;
}

// where the first limb of a wing is defined in a subject type
location definition(arena *a, typer *u, noun sut, noun want) {
  parser *p = u->p;
  location loc = {0};
  u->lastface = 0;
  u->bridged = 0;
  noun r = fond(u, sut, "read", want);
  if (!r || isatom(r)) return loc;
  if (atomis(hd(r), 1) && u->bridged) r = u->bridged;
  noun name = isatom(hd(want)) ? hd(want)
             : iscell(tl(hd(want))) && iscell(tl(tl(hd(want)))) ? tl(tl(tl(hd(want)))) : nul;
  if (atomis(hd(r), 0)) {
    noun opal = tl(tl(r));
    if (atomis(hd(opal), 1)) {
      // an arm
      nouns hag = {0};
      settap(tl(tl(opal)), &hag, p->a);
      if (!hag.len) return loc;
      armloc(a, tl(tl(hag.data[0])), name, &loc);
      return loc;
    }
  }
  // a leg or an alias: the spot that made the face, or the mold
  noun ctx = u->lastface;
  if (tagis(ctx, "spot")) {
    noun spot = tl(ctx);
    if (!srcloc(a, srcbywer(hd(spot)), &loc)) return loc;
    size s = hairoffset(loc.src, spotfrom(spot)), e = hairoffset(loc.src, spotto(spot));
    loc.at = nameinspan(loc.src, s, e, name);
    loc.ok = 1;
  } else if (tagis(ctx, "made")) {
    // in the arm of the mold, from its name on
    i32 budget = 4096;
    noun nam = tl(tl(ctx));
    noun arm = armin(hd(tl(ctx)), nam, &budget);
    if (!arm || !armloc(a, arm, nam, &loc)) return (location){0};
    loc.at = nameinspan(loc.src, loc.at + alen(nam), armend(loc.src, loc.at), name);
  }
  return loc;
}

// whether the name at an offset is being defined there: an arm name,
// a face bound by a rune, or a name followed by = or / in a skin
b32 defsite(s8 src, size at, size *start) {
  if (at >= src.len || !namechar(src.buf[at])) return 0;
  size s = at, e = at;
  while (s > 0 && namechar(src.buf[s-1])) s--;
  while (e < src.len && namechar(src.buf[e])) e++;
  *start = s;
  if (e < src.len && (src.buf[e] == '=' || src.buf[e] == '/')) return 1;
  // the sample names of a mold builder: |$  [a b]
  size j = s;
  while (j > 0 && (namechar(src.buf[j-1]) || src.buf[j-1] == ' ' || src.buf[j-1] == '[')) j--;
  if (j >= 2 && src.buf[j-2] == '|' && src.buf[j-1] == '$') return 1;
  size i = s;
  while (i > 0 && src.buf[i-1] == ' ') i--;
  if (i == s || i < 2) return 0;
  u8 a = src.buf[i-2], b = src.buf[i-1];
  if (a == '+' && (b == '+' || b == '$' || b == '*')) return 1;
  if (a == '$' && b == '=') return 1;
  if (a == '=' && (b == '/' || b == '|' || b == '^' || b == '*' || b == ';' || b == '+')) return 1;
  return 0;
}
typedef struct {
  b32   ok;
  char *path;   // the file of the definition
  hair  at;     // its line and byte column
  s8    src;    // the text of that file, when known
  s8    why;    // the reason there is none
} deflookup;

deflookup lookupfail(s8 why) {
  deflookup r = {0};
  r.why = why;
  return r;
}

deflookup lookupfile(arena *a, char *path) {
  deflookup r = {0};
  r.ok = 1;
  r.path = normpath(a, path);
  r.at.line = 1;
  r.at.col = 1;
  return r;
}

// a file opened, its kernel made first, and parsed, recovering from
// syntax errors; 0 with why if it can't be. With toks, only for them:
// the nouns of the parse are loose, and gone after
// a file parsed for highlighting only, all of it loose, without the
// kernel: so it's quick even when the kernel isn't there yet, and leaves
// the heap as it was, empty if it was, for the kernel's cache
b32 tokenfile(arena *a, char *path, lspfile *f, spans *toks) {
  loosebegin(a);
  b32 r = openfile(a, path, f) && parsefile(a, f, toks);
  looseend();
  return r;
}

b32 loadfile(arena *a, char *path, lspfile *f, spans *toks, s8 *why) {
  char *sys = sysfor(a, path);
  bufout err[1] = {{new(a, u8, 1 << 12), 0, 1 << 12, -1, 0}};
  if (sys && !kernelfor(a, sys, err)) {
    *why = S("cannot build the kernel");
    return 0;
  }
  if (!openfile(a, path, f)) {
    *why = S("cannot read the file");
    return 0;
  }
  if (!sys && !f->desk) f->kernel = 0;
  if (toks) loosebegin(a);
  f->gen = parsefile(a, f, toks);
  if (toks) looseend();
  if (!f->gen) {
    *why = S("cannot parse the file");
    return 0;
  }
  return 1;
}

// the definition of the wing at a position, 1-based with byte columns
deflookup finddef(arena *a, char *path, size line, size col) {
  lspfile f;
  s8 why;
  if (!loadfile(a, path, &f, 0, &why)) return lookupfail(why);
  if (!kern.sys) return lookupfail(S("no desk with sys/hoon.hoon found"));
  parser q = newparser(a, f.src);
  size cur = hairoffset(f.src, (hair){line, col});
  // an import in the header: the imported file
  for (size i = 0; i < f.deps.len; i++) {
    import *d = &f.deps.data[i];
    char *target = 0;
    b32 hit = 0;
    for (i32 j = 0; j < 3; j++) {
      if (d->jumps[j][0] <= cur && cur < d->jumps[j][1]) {
        hit = 1;
        target = d->targets[j];
      }
    }
    if (!hit && d->at <= cur && cur < d->end) {
      hit = 1;
      target = d->kind == dep_noun ? d->targets[0] : d->path;
    }
    if (!hit) continue;
    if (!target || !fileexists(a, target)) return lookupfail(S("imported file not found"));
    return lookupfile(a, target);
  }
  bufout err[1] = {{new(a, u8, 1 << 12), 0, 1 << 12, -1, 0}};
  noun sut = filesubject(a, &f, err);
  if (!sut) sut = kern.subs[5];
  hair at = {line, col};
  noun best = 0;
  innermost(f.gen, f.wer, at, &best);
  size spanstart = best ? hairoffset(f.src, spotfrom(best)) : cur + 1;
  size which = 0, self;
  noun full = wingat(&q, cur, spanstart, &which);
  if (!full) return lookupfail(S("no wing at the cursor"));
  typer u = {0};
  u.p = &q;
  u.spots = 1;
  walker w = {0};
  w.u = &u;
  w.wer = f.wer;
  w.at = at;
  w.full = full;
  w.limb = which;
  w.best = best;
  gwalk(&w, sut, f.gen);
  location loc = {0};
  if (w.sut) loc = definition(a, &u, w.sut, w.want);
  if (!loc.ok && defsite(f.src, cur, &self)) {
    loc.path = f.path;
    loc.src = f.src;
    loc.at = self;
    loc.ok = 1;
  }
  if (!loc.ok) {
    return lookupfail(w.sut ? S("definition not found") : S("cannot find the subject at the cursor"));
  }
  deflookup r = {0};
  r.ok = 1;
  r.path = loc.path;
  r.at = hairin(loc.src, loc.at);
  r.src = loc.src;
  return r;
}

// -b FILE RUNS LINE COL: the fastest of some definition lookups
i32 benchdef(arena *a, bufout *out, bufout *err, char *path, size runs, size line, size col) {
  u64 best = MAXUINT64;
  deflookup r = {0};
  for (size i = 0; i < runs; i++) {
    byte *mark = a->beg;
    u64 t = cycles();
    r = finddef(a, path, line, col);
    t = cycles() - t;
    best = MIN(best, t);
    a->beg = mark;
  }
  if (!r.ok) {
    append(err, r.why);
    append(err, S("\n"));
    return 1;
  }
  u64 hz = cyclefreq();
  appendsize(out, (size)best);
  append(out, hz ? S(" ticks, ") : S(" cycles\n"));
  if (hz) {
    appendsize(out, (size)(best * 1000000 / hz));
    append(out, S(" us\n"));
  }
  return 0;
}

// -b FILE [RUNS]: parse a file as the language server does for
// highlighting, and print the fastest of some runs
i32 bench(arena *a, bufout *out, bufout *err, char *path, size runs) {
  lspfile f;
  s8 why;
  if (!loadfile(a, path, &f, 0, &why)) {
    append(err, why);
    append(err, S("\n"));
    return 1;
  }
  u64 best = MAXUINT64;
  size ntoks = 0, ncells = 0;
  for (size i = 0; i < runs; i++) {
    byte *mark = a->beg;
    u32 cells = H.ncells;
    spans toks = {0};
    u64 t = cycles();
    loosebegin(a);
    noun gen = parsefile(a, &f, &toks);
    looseend();
    t = cycles() - t;
    if (!gen) {
      append(err, S("cannot parse file\n"));
      return 1;
    }
    best = MIN(best, t);
    ntoks = toks.len;
    if (!i) ncells = H.ncells - cells;
    a->beg = mark;
  }
  u64 hz = cyclefreq();
  appendsize(out, (size)best);
  append(out, hz ? S(" ticks, ") : S(" cycles, "));
  if (hz) {
    appendsize(out, (size)(best * 1000000 / hz));
    append(out, S(" us, "));
    appendsize(out, (size)(best * 1000000000 / hz / (u64)MAX(f.src.len, 1)));
    append(out, S(" ns/byte, "));
  } else {
    appendsize(out, (size)(best * 100 / (u64)MAX(f.src.len, 1)));
    append(out, S(" cycles/100 bytes, "));
  }
  appendsize(out, f.src.len);
  append(out, S(" bytes, "));
  appendsize(out, ncells);
  append(out, S(" cells, "));
  appendsize(out, ntoks);
  append(out, S(" tokens\n"));
  return 0;
}

// -d FILE LINE COL: print the definition of the wing at a position
i32 gotodef(arena *a, bufout *out, bufout *err, char *path, size line, size col) {
  deflookup r = finddef(a, path, line, col);
  if (!r.ok) {
    append(err, r.why);
    append(err, S("\n"));
    return 1;
  }
  append(out, (s8){(u8*)r.path, alen(atomcstr(a, r.path))});
  append(out, S(":"));
  appendsize(out, r.at.line);
  append(out, S(":"));
  appendsize(out, r.at.col);
  append(out, S("\n"));
  return 0;
}
// Diagnostics: a file compiled as the compiler compiles it, and what
// goes wrong in it. Compiling stops at the first error, so there's one
// at most

// a place related to a diagnostic: where a type in a nest-fail is from
typedef struct {
  char *path;
  hair  at;     // 1-based, byte columns
  s8    message;
} related;

typedef struct {
  hair    from;   // 1-based, byte columns
  hair    to;
  s8      message;
  related rel[3];
  i32     nrel;
} diagnostic;

// text without its last newline
s8 chomp(bufout *b) {
  s8 s = {b->buf, b->len};
  while (s.len && (s.buf[s.len-1] == '\n' || s.buf[s.len-1] == ' ')) s.len--;
  return s;
}

// what stops a diagnosis to let a request through, or 0
b32 (*diagstop)(void);

i32 diagnosex(arena *a, char *path, diagnostic *d);

// what's wrong with a file, in *d; whether there is anything, or -1 if
// the file can't be read, or -2 if it was stopped by diagstop, to be
// done again
i32 diagnose(arena *a, char *path, diagnostic *d) {
  d->nrel = 0;
  i32 r = diagnosex(a, path, d);
  interrupted = 0;
  if (aborted) {
    aborted = 0;
    return -2;
  }
  return r;
}

i32 diagnosex(arena *a, char *path, diagnostic *d) {
  char *sys = sysfor(a, path);
  bufout err[1] = {{new(a, u8, 1 << 16), 0, 1 << 16, -1, 0}};
  if (sys && !kernelfor(a, sys, err)) {
    d->from = d->to = (hair){1, 1};
    d->message = chomp(err);
    d->nrel = 0;
    return 1;
  }
  // the kernel is made whole, as it's slow to make again; the rest may
  // stop for a request
  interrupted = diagstop;
  lspfile f;
  if (!openfile(a, path, &f)) return -1;
  if (!sys && !f.desk) f.kernel = 0;
  // as it is, without recovering from syntax errors
  size off, budget = -1;
  if (!(f.gen = parsesrc(a, &f, f.src, &off, 0, &budget))) {
    d->from = d->to = hairin(f.src, off);
    d->to.col++;
    d->message = S("syntax error");
    return 1;
  }
  noun sut = filesubject(a, &f, err);
  if (!sut) {
    // an import that can't be built: what ford said, at the top
    d->from = (hair){1, 1};
    d->to = (hair){2, 1};
    d->message = chomp(err);
    return 1;
  }
  parser q = newparser(a, f.src);
  q.file = f.file;
  q.wer = f.wer;
  minter m = {0};
  m.u.p = &q;
  m.vet = 1;
  errnew(errnone);
  if (mint(&m, sut, atomcstr(a, "noun"), f.gen)) return 0;
  noun spot = hcerr.spot;
  if (spot && hd(spot) == f.wer) {
    d->from = spotfrom(spot);
    d->to = spotto(spot);
  } else {
    d->from = (hair){1, 1};
    d->to = (hair){2, 1};
  }
  bufout msg[1] = {{new(a, u8, 1 << 16), 0, 1 << 16, -1, 0}};
  errwhat(msg, &m);
  errdetail(msg, &m);
  d->message = chomp(msg);
  // and where the types of a nest-fail are from, to go to
  errsite es[3];
  i32 n = errsites(&m, es);
  d->nrel = 0;
  for (i32 i = 0; i < n; i++) {
    if (!es[i].at.file) continue;
    related *r = &d->rel[d->nrel++];
    r->path = srcfiles.data[es[i].at.file].name;
    r->at = (hair){es[i].at.line, es[i].at.col};
    bytes t = {0};
    for (char *c = es[i].what; *c; c++) *push(&t, a) = (u8)*c;
    *push(&t, a) = ':';
    *push(&t, a) = ' ';
    for (size k = 0; k < es[i].type.len; k++) *push(&t, a) = es[i].type.buf[k];
    if (es[i].at.name) {
      for (char *c = " (+"; *c; c++) *push(&t, a) = (u8)*c;
      for (size k = 0; k < alen(es[i].at.name); k++) *push(&t, a) = abyte(es[i].at.name, k);
      *push(&t, a) = ')';
    }
    r->message = (s8){t.data, t.len};
  }
  return 1;
}

// -e FILE: what's wrong with a file, as path:line:col: message
i32 lint(arena *a, bufout *out, bufout *err, char *path) {
  diagnostic d;
  i32 r = diagnose(a, path, &d);
  if (r < 0) {
    append(err, S("cannot read file\n"));
    return 2;
  }
  if (!r) return 0;
  append(out, (s8){(u8*)path, (size)__builtin_strlen(path)});
  append(out, S(":"));
  appendsize(out, d.from.line);
  append(out, S(":"));
  appendsize(out, d.from.col);
  append(out, S(": "));
  append(out, d.message);
  append(out, S("\n"));
  return 1;
}

// Output

void appendline(bufout *b, u8 *buf, size len, hair h) {
  size line = 1, i = 0;
  for (; i < len && line < h.line; i++) {
    if (buf[i] == '\n') line++;
  }
  size end = i;
  while (end < len && buf[end] != '\n') end++;
  s8 s = {buf + i, end - i};
  append(b, s);
  append(b, S("\n"));
  for (size j = 1; j < h.col; j++) append(b, S(" "));
  append(b, S("^\n"));
}

// name characters at an error that look like whitespace but aren't
void appendbadchar(bufout *b, u8 *buf, size len, hair h) {
  size line = 1, i = 0;
  for (; i < len && line < h.line; i++) {
    if (buf[i] == '\n') line++;
  }
  i += h.col - 1;
  if (i >= len) return;
  u8 c = buf[i];
  if (c == '\t') {
    append(b, S("unexpected tab"));
  } else if (c == '\r') {
    append(b, S("unexpected carriage return"));
  } else if (c == 0xc2 && i + 1 < len && buf[i+1] == 0xa0) {
    append(b, S("unexpected non-breaking space (U+00A0)"));
  } else if (c < 32 || c == 127) {
    append(b, S("unexpected control character 0x"));
    u8 hx[2] = {"0123456789abcdef"[c >> 4], "0123456789abcdef"[c & 15]};
    append(b, (s8){hx, 2});
  } else {
    return;
  }
  append(b, S(", only spaces and newlines are whitespace in hoon\n"));
}
// Language server. JSON-RPC over stdin and stdout with Content-Length
// framing, answering textDocument/definition and hover and tracking open
// documents.

// JSON values, just enough to read requests
enum { json_null, json_bool, json_num, json_str, json_arr, json_obj };

typedef struct json json;
struct json {
  i32   type;
  s8    raw;    // the text of the value
  s8    str;    // unescaped, for strings
  s8    key;    // unescaped, for object members
  json *child;
  json *next;
};

void jsonws(s8 in, size *pos) {
  while (*pos < in.len && (in.buf[*pos] == ' ' || in.buf[*pos] == '\t'
                           || in.buf[*pos] == '\n' || in.buf[*pos] == '\r')) {
    (*pos)++;
  }
}

i32 hexdigit(u8 c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

size utf8put(u8 *out, u32 c) {
  if (c < 0x80) {
    out[0] = (u8)c;
    return 1;
  }
  if (c < 0x800) {
    out[0] = (u8)(0xc0 | (c >> 6));
    out[1] = (u8)(0x80 | (c & 0x3f));
    return 2;
  }
  if (c < 0x10000) {
    out[0] = (u8)(0xe0 | (c >> 12));
    out[1] = (u8)(0x80 | ((c >> 6) & 0x3f));
    out[2] = (u8)(0x80 | (c & 0x3f));
    return 3;
  }
  out[0] = (u8)(0xf0 | (c >> 18));
  out[1] = (u8)(0x80 | ((c >> 12) & 0x3f));
  out[2] = (u8)(0x80 | ((c >> 6) & 0x3f));
  out[3] = (u8)(0x80 | (c & 0x3f));
  return 4;
}

// a string at pos, just after its opening quote
b32 jsonstr(arena *a, s8 in, size *pos, s8 *out) {
  size end = *pos;
  while (end < in.len && in.buf[end] != '"') end += in.buf[end] == '\\' ? 2 : 1;
  u8 *buf = new(a, u8, end - *pos + 1);
  size len = 0;
  while (*pos < in.len) {
    u8 c = in.buf[(*pos)++];
    if (c == '"') {
      out->buf = buf;
      out->len = len;
      return 1;
    }
    if (c != '\\') {
      buf[len++] = c;
      continue;
    }
    if (*pos >= in.len) return 0;
    c = in.buf[(*pos)++];
    switch (c) {
    case 'n': buf[len++] = '\n'; break;
    case 't': buf[len++] = '\t'; break;
    case 'r': buf[len++] = '\r'; break;
    case 'b': buf[len++] = '\b'; break;
    case 'f': buf[len++] = '\f'; break;
    case 'u': {
      u32 v = 0;
      for (i32 i = 0; i < 4; i++) {
        i32 h = *pos < in.len ? hexdigit(in.buf[(*pos)++]) : -1;
        if (h < 0) return 0;
        v = v << 4 | (u32)h;
      }
      // a surrogate pair
      if (v >= 0xd800 && v < 0xdc00 && *pos + 6 <= in.len
          && in.buf[*pos] == '\\' && in.buf[*pos+1] == 'u') {
        u32 w = 0;
        b32 ok = 1;
        for (i32 i = 0; i < 4; i++) {
          i32 h = hexdigit(in.buf[*pos + 2 + i]);
          if (h < 0) ok = 0;
          w = w << 4 | (u32)h;
        }
        if (ok && w >= 0xdc00 && w < 0xe000) {
          v = 0x10000 + ((v - 0xd800) << 10) + (w - 0xdc00);
          *pos += 6;
        }
      }
      len += utf8put(buf + len, v);
      break;
    }
    default: buf[len++] = c;
    }
  }
  return 0;
}

json *jsonparse(arena *a, s8 in, size *pos, i32 depth) {
  if (depth > 64) return 0;
  jsonws(in, pos);
  if (*pos >= in.len) return 0;
  json *v = new(a, json, 1);
  size start = *pos;
  u8 c = in.buf[*pos];
  if (c == '{' || c == '[') {
    b32 obj = c == '{';
    v->type = obj ? json_obj : json_arr;
    (*pos)++;
    json **tail = &v->child;
    jsonws(in, pos);
    if (*pos < in.len && in.buf[*pos] == (obj ? '}' : ']')) {
      (*pos)++;
    } else {
      for (;;) {
        s8 key = {0};
        if (obj) {
          jsonws(in, pos);
          if (*pos >= in.len || in.buf[*pos] != '"') return 0;
          (*pos)++;
          if (!jsonstr(a, in, pos, &key)) return 0;
          jsonws(in, pos);
          if (*pos >= in.len || in.buf[*pos] != ':') return 0;
          (*pos)++;
        }
        json *e = jsonparse(a, in, pos, depth + 1);
        if (!e) return 0;
        e->key = key;
        *tail = e;
        tail = &e->next;
        jsonws(in, pos);
        if (*pos >= in.len) return 0;
        u8 d = in.buf[(*pos)++];
        if (d == ',') continue;
        if (d == (obj ? '}' : ']')) break;
        return 0;
      }
    }
  } else if (c == '"') {
    v->type = json_str;
    (*pos)++;
    if (!jsonstr(a, in, pos, &v->str)) return 0;
  } else if (c == 't' || c == 'f' || c == 'n') {
    v->type = c == 'n' ? json_null : json_bool;
    while (*pos < in.len && in.buf[*pos] >= 'a' && in.buf[*pos] <= 'z') (*pos)++;
  } else {
    v->type = json_num;
    while (*pos < in.len && ((in.buf[*pos] >= '0' && in.buf[*pos] <= '9') || in.buf[*pos] == '-'
                             || in.buf[*pos] == '+' || in.buf[*pos] == '.'
                             || in.buf[*pos] == 'e' || in.buf[*pos] == 'E')) {
      (*pos)++;
    }
    if (*pos == start) return 0;
  }
  v->raw.buf = in.buf + start;
  v->raw.len = *pos - start;
  return v;
}

json *jsonget(json *v, char *key) {
  if (!v || v->type != json_obj) return 0;
  for (json *e = v->child; e; e = e->next) {
    size n = 0;
    while (key[n]) n++;
    if (e->key.len != n) continue;
    b32 same = 1;
    for (size i = 0; i < n; i++) same &= e->key.buf[i] == (u8)key[i];
    if (same) return e;
  }
  return 0;
}

size jsonsize(json *v) {
  if (!v || v->type != json_num) return 0;
  size n = 0;
  for (size i = 0; i < v->raw.len && v->raw.buf[i] >= '0' && v->raw.buf[i] <= '9'; i++) {
    n = n*10 + (v->raw.buf[i] - '0');
  }
  return n;
}

b32 s8eqc(s8 s, char *c) {
  size n = 0;
  while (c[n]) n++;
  if (s.len != n) return 0;
  for (size i = 0; i < n; i++) {
    if (s.buf[i] != (u8)c[i]) return 0;
  }
  return 1;
}

// JSON output into a growing buffer

void emit(arena *a, bytes *b, s8 s) {
  for (size i = 0; i < s.len; i++) *push(b, a) = s.buf[i];
}

void emitstr(arena *a, bytes *b, s8 s) {
  s8 hex = S("0123456789abcdef");
  *push(b, a) = '"';
  for (size i = 0; i < s.len; i++) {
    u8 c = s.buf[i];
    if (c == '"' || c == '\\') {
      *push(b, a) = '\\';
      *push(b, a) = c;
    } else if (c < 0x20) {
      emit(a, b, S("\\u00"));
      *push(b, a) = hex.buf[c >> 4];
      *push(b, a) = hex.buf[c & 15];
    } else {
      *push(b, a) = c;
    }
  }
  *push(b, a) = '"';
}

void emitsize(arena *a, bytes *b, size n) {
  u8 tmp[32];
  size len = 0;
  do {
    tmp[len++] = (u8)('0' + n % 10);
    n /= 10;
  } while (n);
  while (len) *push(b, a) = tmp[--len];
}

// URIs and positions

// a file:// URI to a path
char *uripath(arena *a, s8 uri) {
  size i = 0;
  if (uri.len >= 7 && s8eqc((s8){uri.buf, 7}, "file://")) i = 7;
  // file:///C:/... on windows
  if (i + 3 <= uri.len && uri.buf[i] == '/' && uri.buf[i+2] == ':') i++;
  if (i + 5 <= uri.len && uri.buf[i] == '/' && uri.buf[i+2] == '%'
      && uri.buf[i+3] == '3' && (uri.buf[i+4] == 'A' || uri.buf[i+4] == 'a')) {
    i++;
  }
  char *r = new(a, char, uri.len - i + 1);
  size len = 0;
  for (; i < uri.len; i++) {
    u8 c = uri.buf[i];
    if (c == '%' && i + 2 < uri.len && hexdigit(uri.buf[i+1]) >= 0 && hexdigit(uri.buf[i+2]) >= 0) {
      c = (u8)(hexdigit(uri.buf[i+1]) * 16 + hexdigit(uri.buf[i+2]));
      i += 2;
    }
    r[len++] = (char)c;
  }
  r[len] = 0;
  return r;
}

void emituri(arena *a, bytes *b, char *path) {
  s8 hex = S("0123456789ABCDEF");
  emit(a, b, S("\"file://"));
  if (path[0] != '/') *push(b, a) = '/';
  for (size i = 0; path[i]; i++) {
    u8 c = (u8)path[i];
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')
        || c == '-' || c == '.' || c == '_' || c == '~' || c == '/') {
      *push(b, a) = c;
    } else {
      *push(b, a) = '%';
      *push(b, a) = hex.buf[c >> 4];
      *push(b, a) = hex.buf[c & 15];
    }
  }
  *push(b, a) = '"';
}

// the start of a 0-based line
size linestartof(s8 src, size line) {
  size i = 0;
  for (size l = 0; l < line && i < src.len; i++) {
    if (src.buf[i] == '\n') l++;
  }
  return i;
}

// a UTF-16 column on a line to a byte column, both 0-based
size utf16tobyte(s8 src, size line, size ch) {
  size s = linestartof(src, line);
  size i = s, units = 0;
  while (i < src.len && src.buf[i] != '\n' && units < ch) {
    u8 c = src.buf[i];
    size n = c < 0x80 ? 1 : c < 0xe0 ? 2 : c < 0xf0 ? 3 : 4;
    units += n == 4 ? 2 : 1;
    i += n;
  }
  return i - s;
}

// a byte column on a line to a UTF-16 column, both 0-based
size bytetoutf16(s8 src, size line, size col) {
  size s = linestartof(src, line);
  size units = 0;
  for (size i = s; i < s + col && i < src.len && src.buf[i] != '\n';) {
    u8 c = src.buf[i];
    size n = c < 0x80 ? 1 : c < 0xe0 ? 2 : c < 0xf0 ? 3 : 4;
    units += n == 4 ? 2 : 1;
    i += n;
  }
  return units;
}

// Documents

void setdoc(arena *perm, arena *scratch, char *path, s8 text) {
  document *d = finddoc(scratch, path);
  if (!d) {
    d = push(opendocs, perm);
    d->path = normpath(perm, path);
    d->buf = 0;
    d->len = d->cap = 0;
  }
  if (text.len > d->cap) {
    d->cap = text.len * 2 + 64;
    d->buf = new(perm, u8, d->cap);
  }
  copy((byte*)d->buf, (byte*)text.buf, text.len);
  d->len = text.len;
}

void closedoc(arena *scratch, char *path) {
  document *d = finddoc(scratch, path);
  if (!d) return;
  *d = opendocs->data[--opendocs->len];
}

// Messages

typedef struct {
  u8  *buf;
  size len;
  size pos;
  b32  eof;
} reader;

b32 fill(reader *r) {
  if (r->eof) return 0;
  if (r->pos > 0) {
    copy((byte*)r->buf, (byte*)r->buf + r->pos, r->len - r->pos);
    r->len -= r->pos;
    r->pos = 0;
  }
  size n = osread(0, r->buf + r->len, (1 << 16) - r->len);
  if (n <= 0) {
    r->eof = 1;
    return 0;
  }
  r->len += n;
  return 1;
}

// the next message body, read into the arena
b32 readmessage(arena *a, reader *r, s8 *body) {
  size length = 0;
  b32 have = 0;
  // headers, up to an empty line
  for (;;) {
    size i = r->pos;
    while (i < r->len && r->buf[i] != '\n') i++;
    if (i >= r->len) {
      if (r->len - r->pos >= (1 << 16) - 1 || !fill(r)) return 0;
      continue;
    }
    s8 line = {r->buf + r->pos, i - r->pos};
    if (line.len && line.buf[line.len-1] == '\r') line.len--;
    r->pos = i + 1;
    if (!line.len) {
      if (have) break;
      continue;
    }
    s8 name = S("Content-Length:");
    if (line.len > name.len) {
      b32 match = 1;
      for (size k = 0; k < name.len; k++) {
        u8 c = line.buf[k];
        u8 d = name.buf[k];
        if (c >= 'A' && c <= 'Z') c += 32;
        if (d >= 'A' && d <= 'Z') d += 32;
        match &= c == d;
      }
      if (match) {
        // digits between spaces, else the header doesn't count
        size k = name.len, n = 0;
        while (k < line.len && line.buf[k] == ' ') k++;
        size digits = k;
        for (; k < line.len && line.buf[k] >= '0' && line.buf[k] <= '9'; k++) {
          if (n <= (size)1 << 40) n = n*10 + (line.buf[k] - '0');
        }
        b32 ok = k > digits;
        while (k < line.len && line.buf[k] == ' ') k++;
        if (ok && k == line.len) {
          length = n;
          have = 1;
        }
      }
    }
  }
  // a body too big for the arena is read past and comes back empty
  b32 keep = length < (a->end - a->beg) / 2;
  body->buf = keep ? new(a, u8, length + 1) : new(a, u8, 1);
  body->len = keep ? length : 0;
  size got = 0;
  while (got < length) {
    if (r->pos == r->len && !fill(r)) return 0;
    size n = MIN(length - got, r->len - r->pos);
    if (keep) copy((byte*)body->buf + got, (byte*)r->buf + r->pos, n);
    got += n;
    r->pos += n;
  }
  return 1;
}

void sendmessage(bufout *out, bytes *body) {
  append(out, S("Content-Length: "));
  appendsize(out, body->len);
  append(out, S("\r\n\r\n"));
  append(out, (s8){body->data, body->len});
  flush(out);
}

void respond(arena *a, bufout *out, json *id, s8 result) {
  bytes b = {0};
  emit(a, &b, S("{\"jsonrpc\":\"2.0\",\"id\":"));
  emit(a, &b, id->raw);
  emit(a, &b, S(",\"result\":"));
  emit(a, &b, result);
  emit(a, &b, S("}"));
  sendmessage(out, &b);
}

void respondmissing(arena *a, bufout *out, json *id) {
  bytes b = {0};
  emit(a, &b, S("{\"jsonrpc\":\"2.0\",\"id\":"));
  emit(a, &b, id->raw);
  emit(a, &b, S(",\"error\":{\"code\":-32601,\"message\":\"method not found\"}}"));
  sendmessage(out, &b);
}

// the definition at the position in a request, with the text of its file
deflookup lookupat(arena *a, json *params, s8 *dst) {
  json *td = jsonget(params, "textDocument");
  json *uri = jsonget(td, "uri");
  json *position = jsonget(params, "position");
  if (!uri || uri->type != json_str || !position) return lookupfail(S("no position"));
  size line = jsonsize(jsonget(position, "line"));
  size ch = jsonsize(jsonget(position, "character"));
  char *path = uripath(a, uri->str);
  s8 src;
  if (!readsource(a, path, &src)) return lookupfail(S("cannot read the file"));
  size col = utf16tobyte(src, line, ch);
  deflookup r = finddef(a, path, line + 1, col + 1);
  if (!r.ok) return r;
  *dst = r.src;
  if (!dst->buf && !readsource(a, r.path, dst)) dst->len = 0;
  return r;
}

// the Location of a definition, or null
s8 definitionresult(arena *a, json *params) {
  s8 dst = {0};
  deflookup r = lookupat(a, params, &dst);
  if (!r.ok) return S("null");
  size l = r.at.line - 1;
  size c = dst.buf ? bytetoutf16(dst, l, r.at.col - 1) : r.at.col - 1;
  bytes b = {0};
  emit(a, &b, S("{\"uri\":"));
  emituri(a, &b, r.path);
  emit(a, &b, S(",\"range\":{\"start\":{\"line\":"));
  emitsize(a, &b, l);
  emit(a, &b, S(",\"character\":"));
  emitsize(a, &b, c);
  emit(a, &b, S("},\"end\":{\"line\":"));
  emitsize(a, &b, l);
  emit(a, &b, S(",\"character\":"));
  emitsize(a, &b, c);
  emit(a, &b, S("}}}"));
  return (s8){b.data, b.len};
}

// the text to show for a definition: an arm with the body indented under
// it, the top of a whole file, or else the line that makes the name; more
// if it goes on past what's shown
s8 defsnippet(s8 src, hair at, b32 *more) {
  *more = 0;
  size s = linestartof(src, at.line - 1);
  if (s >= src.len) return (s8){0, 0};
  size ind = 0;
  while (s + ind < src.len && src.buf[s+ind] == ' ') ind++;
  u8 c1 = s + ind < src.len ? src.buf[s+ind] : 0;
  u8 c2 = s + ind + 1 < src.len ? src.buf[s+ind+1] : 0;
  b32 arm = c1 == '+' && (c2 == '+' || c2 == '$' || c2 == '*') && (size)at.col > ind + 2;
  b32 file = at.line == 1 && at.col == 1;
  i32 most = arm ? 15 : file ? 10 : 1;
  size e = s;
  size end = s;   // after the last line that isn't blank
  for (i32 n = 0; e < src.len; n++) {
    size l = e;
    while (e < src.len && src.buf[e] != '\n') e++;
    size k = l;
    while (k < e && (src.buf[k] == ' ' || src.buf[k] == '\r')) k++;
    b32 blank = k == e;
    // an arm ends at the first line not indented under it
    if (arm && n && !blank && k - l <= ind) break;
    if (n == most) {
      *more = arm || file;
      break;
    }
    if (!blank) end = e;
    if (e < src.len) e++;
  }
  while (end > s && src.buf[end-1] == '\r') end--;
  return (s8){src.buf + s, end - s};
}

// a Hover with the source of a definition as hoon, and where it is
s8 hoverresult(arena *a, json *params) {
  s8 dst = {0};
  deflookup r = lookupat(a, params, &dst);
  if (!r.ok) return S("null");
  b32 more;
  s8 snip = defsnippet(dst, r.at, &more);
  if (!snip.len) return S("null");
  // a fence longer than any run of backticks in the code
  size run = 0, fence = 3;
  for (size i = 0; i < snip.len; i++) {
    run = snip.buf[i] == '`' ? run + 1 : 0;
    if (run >= fence) fence = run + 1;
  }
  bytes md = {0};
  for (size i = 0; i < fence; i++) *push(&md, a) = '`';
  emit(a, &md, S("hoon\n"));
  // without the indentation of the first line, from every line
  size ind = 0;
  while (ind < snip.len && snip.buf[ind] == ' ') ind++;
  for (size i = 0; i < snip.len;) {
    for (size k = 0; k < ind && i < snip.len && snip.buf[i] == ' '; k++) i++;
    while (i < snip.len && snip.buf[i] != '\n') *push(&md, a) = snip.buf[i++];
    if (i < snip.len) *push(&md, a) = snip.buf[i++];
  }
  emit(a, &md, more ? S("\n...\n") : S("\n"));
  for (size i = 0; i < fence; i++) *push(&md, a) = '`';
  // the file name and line, as file.hoon:734
  size n = 0, k = 0;
  while (r.path[n]) n++;
  for (size i = 0; i < n; i++) if (r.path[i] == '/' || r.path[i] == '\\') k = i + 1;
  emit(a, &md, S("\n"));
  emit(a, &md, (s8){(u8*)r.path + k, n - k});
  emit(a, &md, S(":"));
  emitsize(a, &md, r.at.line);
  bytes b = {0};
  emit(a, &b, S("{\"contents\":{\"kind\":\"markdown\",\"value\":"));
  emitstr(a, &b, (s8){md.data, md.len});
  emit(a, &b, S("}}"));
  return (s8){b.data, b.len};
}

// sort spans longest first, so shorter ones paint over them
void sortspans(arena *a, spans *t) {
  span *tmp = new(a, span, t->len + 1);
  for (size w = 1; w < t->len; w *= 2) {
    for (size lo = 0; lo < t->len; lo += 2*w) {
      size mid = MIN(lo + w, t->len), hi = MIN(lo + 2*w, t->len);
      size i = lo, j = mid, k = lo;
      while (i < mid || j < hi) {
        b32 left = j >= hi || (i < mid && t->data[i].end - t->data[i].start
                                          >= t->data[j].end - t->data[j].start);
        tmp[k++] = left ? t->data[i++] : t->data[j++];
      }
    }
    copy((byte*)t->data, (byte*)tmp, t->len * sizeof(span));
  }
}

// the highlighting of a document, as semantic tokens
s8 semanticresult(arena *a, json *params) {
  json *uri = jsonget(jsonget(params, "textDocument"), "uri");
  if (!uri || uri->type != json_str) return S("null");
  char *path = normpath(a, uripath(a, uri->str));
  lspfile f = {0};
  spans toks = {0};
  if (!tokenfile(a, path, &f, &toks) && !f.src.buf) return S("null");
  // paint each byte with the innermost thing it belongs to
  s8 src = f.src;
  i8 *cls = new(a, i8, src.len + 1);
  for (size i = 0; i < src.len; i++) cls[i] = -1;
  sortspans(a, &toks);
  for (size i = 0; i < toks.len; i++) {
    for (size j = toks.data[i].start; j < toks.data[i].end && j < src.len; j++) {
      cls[j] = (i8)toks.data[i].type;
    }
  }
  // runs of one kind, split at newlines, in relative UTF-16 positions
  bytes b = {0};
  emit(a, &b, S("{\"data\":["));
  size line = 0, col = 0, lastline = 0, lastcol = 0;
  b32 first = 1;
  for (size i = 0; i < src.len;) {
    u8 c = src.buf[i];
    if (c == '\n') {
      line++;
      col = 0;
      i++;
      continue;
    }
    i32 k = cls[i];
    size start = col, len = 0;
    while (i < src.len && src.buf[i] != '\n' && cls[i] == k) {
      u8 d = src.buf[i];
      size n = d < 0x80 ? 1 : d < 0xe0 ? 2 : d < 0xf0 ? 3 : 4;
      len += n == 4 ? 2 : 1;
      i += n;
    }
    col += len;
    if (k < 0) continue;
    if (!first) emit(a, &b, S(","));
    first = 0;
    emitsize(a, &b, line - lastline);
    emit(a, &b, S(","));
    emitsize(a, &b, line == lastline ? start - lastcol : start);
    emit(a, &b, S(","));
    emitsize(a, &b, len);
    emit(a, &b, S(","));
    emitsize(a, &b, (size)k);
    emit(a, &b, S(",0"));
    lastline = line;
    lastcol = start;
  }
  emit(a, &b, S("]}"));
  return (s8){b.data, b.len};
}

// sys and deps from the editor, as {"sys": "...", "deps": ["...", ...]},
// with relative paths from the workspace root
void configure(arena *perm, json *opts, char *root) {
  if (!opts || opts->type != json_obj) return;
  json *nested = jsonget(opts, "hoon");
  if (nested && nested->type == json_obj) opts = nested;
  json *sys = jsonget(opts, "sys");
  json *deps = jsonget(opts, "deps");
  if (sys && sys->type == json_str) setsys(perm, root, sys->str);
  if (deps && deps->type == json_arr) {
    config.deps = 0;
    config.ndeps = 0;
    for (json *e = deps->child; e; e = e->next) {
      if (e->type == json_str) adddep(perm, root, e->str);
    }
    // the desks searched differ, so what was built from them goes
    ndesks = 0;
  }
}

// send what's wrong with a file, or that nothing is; 0 if it was stopped
// for a request, to be done again
b32 publish(arena *a, bufout *out, char *path, b32 closed) {
  diagnostic d;
  i32 n = closed ? 0 : diagnose(a, path, &d);
  if (n == -2) return 0;
  if (n < 0) return 1;
  s8 src = {0};
  if (n) readsource(a, path, &src);
  bytes b = {0};
  emit(a, &b, S("{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/publishDiagnostics\","
                "\"params\":{\"uri\":"));
  emituri(a, &b, path);
  emit(a, &b, S(",\"diagnostics\":["));
  if (n) {
    hair h[2] = {d.from, d.to};
    emit(a, &b, S("{\"range\":{"));
    for (i32 i = 0; i < 2; i++) {
      size l = h[i].line - 1, c = bytetoutf16(src, l, h[i].col - 1);
      emit(a, &b, i ? S(",\"end\":{\"line\":") : S("\"start\":{\"line\":"));
      emitsize(a, &b, l);
      emit(a, &b, S(",\"character\":"));
      emitsize(a, &b, c);
      emit(a, &b, S("}"));
    }
    emit(a, &b, S("},\"severity\":1,\"source\":\"hoon\",\"message\":"));
    emitstr(a, &b, d.message);
    // where the types it's about are from
    emit(a, &b, S(",\"relatedInformation\":["));
    for (i32 i = 0; i < d.nrel; i++) {
      related *r = &d.rel[i];
      s8 rs = {0};
      readsource(a, r->path, &rs);
      size l = r->at.line - 1, c = rs.buf ? bytetoutf16(rs, l, r->at.col - 1) : r->at.col - 1;
      if (i) emit(a, &b, S(","));
      emit(a, &b, S("{\"location\":{\"uri\":"));
      emituri(a, &b, r->path);
      emit(a, &b, S(",\"range\":{\"start\":{\"line\":"));
      emitsize(a, &b, l);
      emit(a, &b, S(",\"character\":"));
      emitsize(a, &b, c);
      emit(a, &b, S("},\"end\":{\"line\":"));
      emitsize(a, &b, l);
      emit(a, &b, S(",\"character\":"));
      emitsize(a, &b, c);
      emit(a, &b, S("}}},\"message\":"));
      emitstr(a, &b, r->message);
      emit(a, &b, S("}"));
    }
    emit(a, &b, S("]}"));
  }
  emit(a, &b, S("]}}"));
  sendmessage(out, &b);
  return 1;
}

// whether there's input on stdin to read, without waiting for it
#ifdef _WIN32
W32(i32) PeekNamedPipe(void *, void *, u32, u32 *, u32 *, u32 *);
W32(void *) GetStdHandle(i32);
b32 osinputready(void) {
  u32 n = 0;
  return PeekNamedPipe(GetStdHandle(-10), 0, 0, 0, &n, 0) && n > 0;
}
#elif defined(__linux__)
b32 osinputready(void) {
  struct { i32 fd; i16 events; i16 revents; } p = {0, 1, 0};   // stdin, POLLIN
  i64 zero[2] = {0, 0};   // a timespec, so not waiting
  return syscall6(sys_ppoll, (i64)&p, 1, (i64)zero, 0, 0, 0) > 0;
}
#else
typedef struct {
  i32 fd;
  i16 events;
  i16 revents;
} pollfd;
i32 poll(pollfd *, usize, i32);
b32 osinputready(void) {
  pollfd p = {0, 1, 0};   // stdin, POLLIN
  return poll(&p, 1, 0) > 0;
}
#endif

// Files waiting for their diagnostics, which are made only when nothing
// else is waiting: compiling a file takes a while, and highlighting,
// hover and definitions shouldn't wait for it

// whether a request is waiting, buffered or on stdin
reader *lspreader;

b32 requestwaiting(void) {
  return lspreader->pos < lspreader->len || osinputready();
}

enum { maxpending = 64 };
char *pending[maxpending];
i32   npending;

void defer(arena *perm, char *path) {
  for (i32 i = 0; i < npending; i++) if (streq(pending[i], path)) return;
  if (npending == maxpending) {
    for (i32 i = 1; i < npending; i++) pending[i-1] = pending[i];
    npending--;
  }
  size n = (size)__builtin_strlen(path);
  char *c = new(perm, char, n + 1);
  copy(c, path, n + 1);
  pending[npending++] = c;
}

void undefer(char *path) {
  for (i32 i = 0; i < npending; i++) {
    if (!streq(pending[i], path)) continue;
    for (i32 k = i + 1; k < npending; k++) pending[k-1] = pending[k];
    npending--;
    return;
  }
}

// run the language server on stdin and stdout
i32 lspmain(arena *a, bufout *out) {
  // documents live in their own arena, requests in the rest
  arena perm = *a;
  size half = (a->end - a->beg) / 8;
  perm.end = perm.beg + half;
  arena scratch = *a;
  scratch.beg += half;
  documents docs = {0};
  opendocs = &docs;
  // files are read as they are in the editor
  srcreader = readsource;
  reader r = {new(&perm, u8, 1 << 16), 0, 0, 0};
  lspreader = &r;
  diagstop = requestwaiting;
  b32 shutdown = 0;
  char *root = 0;   // of the workspace
  u32 live = H.ncells;
  for (;;) {
    byte *mark = scratch.beg;
    // diagnostics, while there's nothing else to do
    while (npending && r.pos >= r.len && !osinputready()) {
      char *path = pending[0];
      b32 done = publish(&scratch, out, path, 0);
      if (done) {
        for (i32 k = 1; k < npending; k++) pending[k-1] = pending[k];
        npending--;
      }
      if (H.ncells > live + (1 << 22)) {
        collectall(&scratch, desks, ndesks, kern.subs, countof(kern.subs));
        live = H.ncells;
      }
      osrelease(mark, scratch.end);
      scratch.beg = mark;
    }
    s8 body;
    if (!readmessage(&scratch, &r, &body)) return 1;
    size pos = 0;
    json *msg = jsonparse(&scratch, body, &pos, 0);
    json *method = jsonget(msg, "method");
    json *id = jsonget(msg, "id");
    json *params = jsonget(msg, "params");
    if (!method || method->type != json_str) {
      osrelease(mark, scratch.end);
      scratch.beg = mark;
      continue;
    }
    s8 m = method->str;
    json *td = jsonget(params, "textDocument");
    json *uri = jsonget(td, "uri");
    char *path = uri && uri->type == json_str ? normpath(&scratch, uripath(&scratch, uri->str)) : 0;
    if (s8eqc(m, "initialize")) {
      json *ruri = jsonget(params, "rootUri");
      json *rpath = jsonget(params, "rootPath");
      if (ruri && ruri->type == json_str) root = normpath(&perm, uripath(&perm, ruri->str));
      else if (rpath && rpath->type == json_str) root = configpath(&perm, 0, rpath->str);
      configure(&perm, jsonget(params, "initializationOptions"), root);
      // a request has an id to answer; sent as a notification, there is
      // nothing to answer
      if (id) respond(&scratch, out, id, S("{\"capabilities\":{\"textDocumentSync\":"
                                   "{\"openClose\":true,\"change\":1,\"save\":{\"includeText\":false}},"
                                   "\"definitionProvider\":true,"
                                   "\"hoverProvider\":true,"
                                   "\"semanticTokensProvider\":{\"legend\":{\"tokenTypes\":"
                                   "[\"comment\",\"string\",\"number\",\"keyword\","
                                   "\"function\",\"variable\",\"type\",\"enumMember\","
                                   "\"operator\"],"
                                   "\"tokenModifiers\":[]},\"full\":true}},"
                                   "\"serverInfo\":{\"name\":\"hoon\"}}"));
    } else if (s8eqc(m, "workspace/didChangeConfiguration")) {
      configure(&perm, jsonget(params, "settings"), root);
    } else if (s8eqc(m, "shutdown")) {
      shutdown = 1;
      if (id) respond(&scratch, out, id, S("null"));
    } else if (s8eqc(m, "exit")) {
      return shutdown ? 0 : 1;
    } else if (s8eqc(m, "textDocument/didOpen")) {
      json *text = jsonget(td, "text");
      if (path && text && text->type == json_str) {
        setdoc(&perm, &scratch, path, text->str);
        forgetfile(&scratch, path);
        defer(&perm, path);
      }
    } else if (s8eqc(m, "textDocument/didChange")) {
      json *changes = jsonget(params, "contentChanges");
      json *last = 0;
      for (json *e = changes ? changes->child : 0; e; e = e->next) last = e;
      json *text = jsonget(last, "text");
      if (path && text && text->type == json_str) {
        setdoc(&perm, &scratch, path, text->str);
        forgetfile(&scratch, path);
      }
    } else if (s8eqc(m, "textDocument/didSave")) {
      if (path) {
        forgetfile(&scratch, path);
        defer(&perm, path);
      }
    } else if (s8eqc(m, "textDocument/didClose")) {
      if (path) {
        closedoc(&scratch, path);
        forgetfile(&scratch, path);
        undefer(path);
        publish(&scratch, out, path, 1);
      }
    } else if (s8eqc(m, "textDocument/semanticTokens/full") && id) {
      respond(&scratch, out, id, semanticresult(&scratch, params));
    } else if (s8eqc(m, "textDocument/definition") && id) {
      respond(&scratch, out, id, definitionresult(&scratch, params));
    } else if (s8eqc(m, "textDocument/hover") && id) {
      respond(&scratch, out, id, hoverresult(&scratch, params));
    } else if (id) {
      respondmissing(&scratch, out, id);
    }
    // what's kept is collected when the heap has grown enough, and a
    // request may touch hundreds of megabytes, which go back
    if (H.ncells > live + (1 << 22)) {
      collectall(&scratch, desks, ndesks, kern.subs, countof(kern.subs));
      live = H.ncells;
    }
    osrelease(mark, scratch.end);
    scratch.beg = mark;
  }
}

// With no arguments, run a language server on stdin and stdout that
// answers go to definition and hover, highlights, and says what's wrong
// with files as they're opened and saved. Otherwise:
//
//   -d FILE LINE COL  print where the wing at that position in FILE is
//                     defined, as path:line:col
//   -e FILE    print what's wrong with FILE, as path:line:col: message
//   -b FILE [RUNS [LINE COL]]  time parsing FILE, or a definition in it
//   -p         parse hoon from stdin like +ream and print the noun
//   -x         the same, fully bracketed with hex atoms, as test/ expects
//   -o         the same, after one +open
//   -t         the type of the hoon against %noun, as +play
//   -k FILE    the mug of the type of the hoon against the type of FILE
//
// --sys DIR and --dep DIR, anywhere, are as sys and deps from an editor.
i32 parsermain(arena *a, i32 argc, char **argv) {
  heapinit(a);
  // the texts of sources, which don't last here, are copied, and the
  // kernel is cached apart from the compiler's
  srccopy = 1;
  chainwho = "hoon-lsp";
  i32 n = 0;
  for (i32 i = 0; i < argc; i++) {
    if (i + 1 < argc && streq(argv[i], "--sys")) {
      s8 dir = {(u8*)argv[i+1], 0};
      while (argv[i+1][dir.len]) dir.len++;
      setsys(a, 0, dir);
      i++;
    } else if (i + 1 < argc && streq(argv[i], "--dep")) {
      s8 dir = {(u8*)argv[i+1], 0};
      while (argv[i+1][dir.len]) dir.len++;
      adddep(a, 0, dir);
      i++;
    } else {
      argv[n++] = argv[i];
    }
  }
  argc = n;
  u8 mode = argc > 1 && argv[1][0] == '-' ? (u8)argv[1][1] : 0;
  b32 raw = mode == 'x' || mode == 'o' || mode == 't';
  i32 cap = 1<<12;

  bufout stdout[1] = {0};
  stdout->fd = 1;
  stdout->cap = cap;
  stdout->buf = new(a, u8, cap);

  bufout stderr[1] = {0};
  stderr->fd = 2;
  stderr->cap = cap;
  stderr->buf = new(a, u8, cap);

  if (argc == 1) return lspmain(a, stdout);

  if (mode == 'b') {
    if (argc < 3) {
      append(stderr, S("usage: -b FILE [RUNS [LINE COL]]\n"));
      flush(stderr);
      return 2;
    }
    size runs = argc > 3 ? parsesize(argv[3]) : 20;
    i32 r = argc > 5 ? benchdef(a, stdout, stderr, argv[2], runs, parsesize(argv[4]), parsesize(argv[5]))
                     : bench(a, stdout, stderr, argv[2], runs);
    flush(stdout);
    flush(stderr);
    return r;
  }

  if (mode == 'd' || mode == 'e') {
    if (argc < (mode == 'd' ? 5 : 3)) {
      append(stderr, mode == 'd' ? S("usage: -d FILE LINE COL\n") : S("usage: -e FILE\n"));
      flush(stderr);
      return 2;
    }
    i32 r = mode == 'e' ? lint(a, stdout, stderr, argv[2])
                        : gotodef(a, stdout, stderr, argv[2], parsesize(argv[3]), parsesize(argv[4]));
    flush(stdout);
    flush(stderr);
    return r;
  }

  noun sut = atomcstr(a, "noun");
  if (mode == 'k') {
    s8 ksrc;
    if (argc < 3 || !osreadfile(a, argv[2], &ksrc)) {
      append(stderr, S("cannot read kernel\n"));
      flush(stderr);
      return 2;
    }
    parser kp = newparser(a, ksrc);
    size kpos = 0;
    noun kgen = vest(&kp, &kpos);
    typer ku = {0};
    ku.p = &kp;
    sut = kgen ? play(&ku, sut, kgen) : 0;
    if (!sut) {
      append(stderr, S("cannot type kernel\n"));
      flush(stderr);
      return 2;
    }
  }

  s8 in = readstdin(a);
  u8 *buf = in.buf;
  size len = in.len;
  parser p = newparser(a, in);
  size pos = 0;
  noun r = vest(&p, &pos);
  if (mode == 't' || mode == 'k') {
    typer u = {0};
    u.p = &p;
    r = r ? play(&u, sut, r) : 0;
    if (!r) {
      append(stdout, S("crash\n"));
      flush(stdout);
      return 1;
    }
    if (mode == 'k') {
      appendmug(stdout, r);
      append(stdout, S("\n"));
      flush(stdout);
      return 0;
    }
  }
  if (mode == 'o') {
    r = r ? hoonopen(&p, r) : 0;
    if (!r) {
      append(stdout, S("crash\n"));
      flush(stdout);
      return 1;
    }
  }

  if (!r) {
    hair h = errhair(&p);
    if (raw) {
      append(stdout, S("err "));
      appendsize(stdout, h.line);
      append(stdout, S(" "));
      appendsize(stdout, h.col);
      append(stdout, S("\n"));
      flush(stdout);
      return 1;
    }
    append(stderr, S("syntax error at line "));
    appendsize(stderr, h.line);
    append(stderr, S(", column "));
    appendsize(stderr, h.col);
    append(stderr, S("\n"));
    appendline(stderr, buf, len, h);
    appendbadchar(stderr, buf, len, h);
    if (spent(&p)) {
      append(stderr, p.deep ? S("gave up: nested too deep\n") : S("gave up: too much backtracking\n"));
    }
    flush(stderr);
    return 1;
  }

  if (raw) {
    appendraw(stdout, r);
  } else {
    appendnoun(stdout, r);
  }
  append(stdout, S("\n"));
  flush(stdout);
  return 0;
}

#ifdef _WIN32
void mainCRTStartup(void) {
  // reserved, not committed until used
  size cap = (size)1 << 36;
  arena a = {0};
  a.beg = VirtualAlloc(0, (usize)cap, 0x2000, 4);
  a.dat = a.beg;
  a.end = a.beg + cap;
  char **argv;
  i32 argc = splitargs(&a, GetCommandLineA(), &argv);
  i32 r = parsermain(&a, argc, argv);
  ExitProcess(r);
}
#else
i32 main(i32 argc, char **argv) {
  // reserved, not touched until used
  size cap = (size)1 << 36;
  arena a = {0};
  a.beg = osreserve(cap);
  if (!a.beg) oom();
  a.dat = a.beg;
  a.end = a.beg + cap;
  return parsermain(&a, argc, argv);
}
#endif
