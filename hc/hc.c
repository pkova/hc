// hc, a Hoon compiler: ../hoon.c does the work, and here is how it is
// run.

#include "../hoon.c"

// Command line

size parsesize(char *s) {
  size n = 0;
  for (; *s >= '0' && *s <= '9'; s++) n = n*10 + (*s - '0');
  return n;
}

// With a flag, compile hoon from stdin against %noun, as
// (~(mint ut %noun) %noun (ream txt)), and print:
//
//   -m         the type, after +burp, and the formula, fully bracketed
//              with hex atoms, as hc/test/ expects
//   -n         the formula alone, the same way
//   -k FILE    the mugs of the type and the formula, against the type
//              of FILE as +play has it
//   -f         the mugs of the type and the formula, for whole files
//   -d AXIS DEPTH  the mugs of the formula's subtrees under AXIS, in
//              hex, to DEPTH levels, or with DEPTH 0 the subtree itself,
//              8 deep
//
// or "crash" where hoon would crash, or "err LINE COL" for a syntax
// error. With a file and no flag, print the formula of the file.
//
// -s FILE, before the flag and as many times as needed, compiles FILE
// against the subject so far and makes its type the subject, as for
// arvo.hoon against hoon.hoon; -e HOON does the same with HOON itself,
// as -e ..part after arvo.hoon gives the subject of lull.hoon. -S FILE
// is -s as arvo compiles its files, parsed as +rain does with the path
// of FILE from its sys directory, like /sys/lull/hoon.
//
// -b compiles each of the inputs on stdin separated by lines of %%%,
// with one line of output for each, sharing the kernel and subjects.
//
// -D DESK compiles files in the desk at DESK through ford, as clay builds
// them, against the subject so far as zuse: the file named last, like
// app/foo.hoon, or with -b each named on a line of stdin.
//
// -c FILE makes hoon.hoon at FILE the compiling kernel, as the one the
// reference runs: see lazemake. Without it, lazy batteries in types are
// stand-ins, and differ from the reference's where they reach a formula.
//
// The kernel and subjects made by -c, -s, -S and -e are cached, in
// $HC_CACHE, $XDG_CACHE_HOME/hc or ~/.cache/hc, and remade only when a
// file they're made from or this compiler changes: see chainpath.
// HC_CACHE=none turns it off.





// compile one input and print what mode asks for; 0 if it compiled
i32 compileone(arena *a, bufout *stdout, bufout *stderr, u8 mode, noun sut, s8 in, char **argv,
               fordenv *ford, noun pax, char *name) {
  parser p = newparser(a, in);
  p.file = srcadd(name, pax, in);
  if (!ford) p.wer = pax;
  b32 quiet = mode == 'q';
  size pos = 0;
  noun gen;
  if (ford) {
    // a desk file: its imports, then its body against them
    noun pil = pilerule(&p, pax);
    gen = pil ? tl(tl(tl(tl(tl(tl(tl(pil))))))) : 0;
    if (pil && !(sut = fordsubject(&p, ford, pil, stderr))) {
      if (!quiet) append(mode ? stdout : stderr, S("crash\n"));
      flush(stdout);
      flush(stderr);
      return 1;
    }
  } else {
    gen = vest(&p, &pos);
  }
  if (!gen) {
    hair h = errhair(&p);
    append(stderr, (s8){(u8*)name, (size)__builtin_strlen(name)});
    append(stderr, S(":"));
    appendsize(stderr, h.line);
    append(stderr, S(":"));
    appendsize(stderr, h.col);
    append(stderr, S(": syntax error\n"));
    flush(stderr);
    if (mode && !quiet) {
      append(stdout, S("err "));
      appendsize(stdout, h.line);
      append(stdout, S(" "));
      appendsize(stdout, h.col);
      append(stdout, S("\n"));
      flush(stdout);
    }
    return 1;
  }

  minter m = {0};
  m.u.p = &p;
  m.u.fan = 0;
  m.vet = 1;
  m.rib = 0;
  errnew(errnone);
  noun r = mint(&m, sut, atomcstr(a, "noun"), gen);
  if (!r) {
    if (!hcerr.spot && !ford) {
      // parsed as ream does, without spots: again with them, to say where
      parser q = newparser(a, in);
      q.bug = 1;
      q.wer = pax ? pax : cons(a, atomcstr(a, name), nul);
      q.file = p.file;
      srcfiles.data[p.file].wer = q.wer;
      size qos = 0;
      noun g = vest(&q, &qos);
      minter n = m;
      n.u.p = &q;
      errnew(errnone);
      if (g && mint(&n, sut, atomcstr(a, "noun"), g)) errnew(errnone);
    }
    errprint(stderr, &m, name);
    if (!quiet) append(mode ? stdout : stderr, S("crash\n"));
    flush(stdout);
    flush(stderr);
    return 1;
  }
  if (quiet) return 0;
  noun typ = burp(&m, hd(r));
  if (mode == 'd') {
    // the mugs of the formula's subtrees, under an axis and to a depth,
    // to find where a large one differs
    // the axis in hex, as it may not fit in a word
    u8 *hx = (u8*)argv[2];
    size hn = (size)__builtin_strlen(argv[2]);
    u8 *hb = new(a, u8, hn/2 + 1);
    for (size i = 0; i < hn; i++) {
      u8 c = hx[hn-1-i];
      u8 v = c >= 'a' ? c - 'a' + 10 : c >= 'A' ? c - 'A' + 10 : c - '0';
      hb[i/2] |= (u8)(v << 4*(i%2));
    }
    noun axis0 = atombytes(a, hb, hn/2 + 1);
    noun at = frag(axis0, tl(r));
    size depth = parsesize(argv[3]);
    if (!depth) {
      if (at) appendrawto(stdout, at, 8);
      append(stdout, S("\n"));
      flush(stdout);
      return 0;
    }
    nouns xs = {0}, ax = {0};
    *push(&xs, a) = at;
    *push(&ax, a) = axis0;
    for (size i = 0; i < xs.len; i++) {
      appendhexatom(stdout, ax.data[i]);
      append(stdout, S(" "));
      appendsize(stdout, xs.data[i] ? (size)mug(xs.data[i]) : 0);
      append(stdout, S("\n"));
      if (!xs.data[i] || isatom(xs.data[i]) || atombits(ax.data[i]) > depth + atombits(ax.data[0])) continue;
      *push(&xs, a) = hd(xs.data[i]);
      *push(&ax, a) = peg(&(parser){.a = a}, ax.data[i], atomu64(a, 2));
      *push(&xs, a) = tl(xs.data[i]);
      *push(&ax, a) = peg(&(parser){.a = a}, ax.data[i], atomu64(a, 3));
    }
    flush(stdout);
    return 0;
  }
  if (mode == 'm') {
    appendraw(stdout, cons(a, typ, tl(r)));
  } else if (mode == 'n' || !mode) {
    appendraw(stdout, tl(r));
  } else {
    appendmug(stdout, typ);
    append(stdout, S(" "));
    appendmug(stdout, tl(r));
  }
  append(stdout, S("\n"));
  flush(stdout);
  return 0;
}


// a desk file named by rel, compiled through ford
i32 compiledesk(arena *a, bufout *stdout, bufout *stderr, u8 mode, fordenv *f, char *rel,
                size len, char **argv) {
  char *rz = new(a, char, len + 1);
  copy(rz, rel, len);
  noun pax = deskpath(a, rz, len);
  s8 src;
  if (!osreadfile(a, deskfile(a, f, pax), &src)) {
    append(stderr, S("cannot read "));
    append(stderr, (s8){(u8*)rz, len});
    append(stderr, S("\n"));
    if (mode != 'q') append(stdout, S("crash\n"));
    flush(stdout);
    flush(stderr);
    return 1;
  }
  return compileone(a, stdout, stderr, mode, f->zuse, src, argv, f, pax, deskfile(a, f, pax));
}


// files of a desk, named from its top, through ford, collecting as it
// goes; the failures added to bad
void deskfiles(arena *a, bufout *stdout, bufout *stderr, u8 mode, fordenv *f, char **rels,
               size n, b32 compact, i32 *bad) {
  // collect what building the subject left, if that hasn't been, and
  // again whenever the files since have made gcfresh cells
  enum { gcfresh = 1 << 22 };
  byte *mark = a->beg;
  if (!compact) {
    collect(a, f);
    a->beg = mark;
  }
  u32 live = H.ncells;
  for (size i = 0; i < n; i++) {
    *bad += compiledesk(a, stdout, stderr, mode, f, rels[i], (size)__builtin_strlen(rels[i]), 0) != 0;
    if (H.ncells > live + gcfresh) {
      mark = a->beg;
      collect(a, f);
      a->beg = mark;
      live = H.ncells;
    }
  }
}

// The interface for people: hc SYS PATH..., SYS the sys directory of a
// desk, with hoon.hoon, arvo.hoon, lull.hoon and zuse.hoon, and each PATH
// a file to compile or a directory with them, all .hoon files under it.
// Each is compiled as clay would in the desk it's in, the nearest
// directory above it with a desk.bill or sys.kelvin, or failing that
// with a lib, sur, mar, app or gen, its imports from there; files in SYS
// as arvo would. What fails is said, and a line at the end says how many.

typedef struct {
  char **data;
  size   len;
  size   cap;
} paths;

char *pathjoin(arena *a, char *dir, char *name) {
  size m = (size)__builtin_strlen(dir), n = (size)__builtin_strlen(name);
  char *r = new(a, char, m + n + 2);
  copy(r, dir, m);
  r[m] = '/';
  copy(r + m + 1, name, n + 1);
  return r;
}

// the directory a path is in, 0 at the top
char *pathup(arena *a, char *path) {
  size n = (size)__builtin_strlen(path);
  while (n > 0 && path[n-1] != '/') n--;
  if (n <= 1) return 0;
  char *r = new(a, char, n);
  copy(r, path, n - 1);
  r[n-1] = 0;
  return r;
}

b32 isfile(arena *a, char *path) {
  byte *mark = a->beg;
  s8 s;
  b32 r = osreadfile(a, path, &s);
  a->beg = mark;
  return r;
}

b32 endswith(char *s, char *t) {
  size m = (size)__builtin_strlen(s), n = (size)__builtin_strlen(t);
  return m >= n && streq(s + m - n, t);
}

i32 strorder(char *x, char *y) {
  while (*x && *x == *y) x++, y++;
  return (u8)*x - (u8)*y;
}

// the .hoon files at a path, all under it if it's a directory, in order
void gather(arena *a, char *path, paths *out) {
  if (!osisdir(path)) {
    *push(out, a) = path;
    return;
  }
  char **names;
  size n = oslistdir(a, path, &names);
  for (size i = 1; i < n; i++) {
    for (size j = i; j > 0 && strorder(names[j-1], names[j]) > 0; j--) {
      char *t = names[j];
      names[j] = names[j-1];
      names[j-1] = t;
    }
  }
  for (size i = 0; i < n; i++) {
    if (names[i][0] == '.') continue;
    char *p = pathjoin(a, path, names[i]);
    if (osisdir(p)) gather(a, p, out);
    else if (endswith(p, ".hoon")) *push(out, a) = p;
  }
}

// the top of the desk a file is in
char *deskroot(arena *a, char *file) {
  char *weak = 0;
  for (char *d = pathup(a, file); d; d = pathup(a, d)) {
    if (isfile(a, pathjoin(a, d, "desk.bill")) || isfile(a, pathjoin(a, d, "sys.kelvin"))) return d;
    if (!weak) {
      char *subs[] = {"lib", "sur", "mar", "app", "gen"};
      for (i32 i = 0; i < countof(subs) && !weak; i++) {
        if (osisdir(pathjoin(a, d, subs[i]))) weak = d;
      }
    }
  }
  return weak ? weak : pathup(a, file);
}

// whether path is dir or under it, and if under, what's after dir/
char *under(char *path, char *dir) {
  size k = 0;
  while (dir[k] && dir[k] == path[k]) k++;
  if (dir[k]) return 0;
  return path[k] == '/' ? path + k + 1 : !path[k] ? path + k : 0;
}

i32 hcuser(arena *a, bufout *stdout, bufout *stderr, char *sysarg, char **args, i32 nargs) {
  cwdpath = osabspath(a, ".");
  char *sys = osabspath(a, sysarg);
  if (sys && !isfile(a, pathjoin(a, sys, "hoon.hoon")) && isfile(a, pathjoin(a, sys, "sys/hoon.hoon"))) {
    sys = pathjoin(a, sys, "sys");
  }
  char *parts[] = {"hoon.hoon", "arvo.hoon", "lull.hoon", "zuse.hoon"};
  for (i32 i = 0; i < countof(parts); i++) {
    if (!sys || !isfile(a, pathjoin(a, sys, parts[i]))) {
      append(stderr, S("hc: no "));
      append(stderr, (s8){(u8*)parts[i], (size)__builtin_strlen(parts[i])});
      append(stderr, S(" in "));
      append(stderr, (s8){(u8*)sysarg, (size)__builtin_strlen(sysarg)});
      append(stderr, S("\n"));
      flush(stderr);
      return 2;
    }
  }
  // the files, each once
  paths files = {0};
  for (i32 i = 0; i < nargs; i++) {
    char *p = osabspath(a, args[i]);
    if (!p) {
      append(stderr, S("hc: no such file: "));
      append(stderr, (s8){(u8*)args[i], (size)__builtin_strlen(args[i])});
      append(stderr, S("\n"));
      flush(stderr);
      return 2;
    }
    gather(a, p, &files);
  }
  // the chain, as arvo builds it
  char *kernel = pathjoin(a, sys, "hoon.hoon");
  char *subjects[] = {kernel, pathjoin(a, sys, "arvo.hoon"), "..part", pathjoin(a, sys, "lull.hoon"),
                      pathjoin(a, sys, "zuse.hoon")};
  i32 exprs[] = {2, 2, 1, 2, 2};
  b32 compact;
  noun sut = chainmake(a, stderr, kernel, subjects, exprs, countof(subjects), 0, &compact);
  if (!sut) {
    flush(stderr);
    return 1;
  }
  i32 bad = 0;
  size done = 0;
  b32 *seen = new(a, b32, files.len + 1);
  for (size i = 0; i < files.len; i++) {
    if (seen[i]) continue;
    char *rel = under(files.data[i], sys);
    if (rel) {
      // in sys: the chain is made of hoon, arvo, lull and zuse, and a
      // vane is compiled against zuse, as arvo does
      seen[i] = 1;
      done++;
      b32 chain = 0;
      for (i32 k = 0; k < countof(parts); k++) chain |= streq(rel, parts[k]);
      if (chain) continue;
      s8 src;
      if (!osreadfile(a, files.data[i], &src)) {
        append(stderr, S("hc: cannot read "));
        append(stderr, shown(files.data[i]));
        append(stderr, S("\n"));
        bad++;
        continue;
      }
      bad += compileone(a, stdout, stderr, 'q', sut, src, 0, 0, syspath(a, files.data[i]),
                        files.data[i]) != 0;
      continue;
    }
    // the rest of the files in the same desk, together, through ford
    char *root = deskroot(a, files.data[i]);
    paths rels = {0};
    for (size j = i; j < files.len; j++) {
      if (seen[j] || under(files.data[j], sys)) continue;
      if (!streq(deskroot(a, files.data[j]), root)) continue;
      seen[j] = 1;
      *push(&rels, a) = under(files.data[j], root);
    }
    fordenv f = {0};
    f.desk = root;
    f.zuse = sut;
    deskfiles(a, stdout, stderr, 'q', &f, rels.data, rels.len, compact, &bad);
    sut = f.zuse;   // moved by collect
    compact = 1;
    done += rels.len;
  }
  if (bad) {
    appendsize(stderr, bad);
    append(stderr, S(" of "));
  }
  appendsize(stderr, (size)done);
  append(stderr, done == 1 ? S(" file") : S(" files"));
  append(stderr, bad ? S(" failed\n") : S(" compiled\n"));
  flush(stdout);
  flush(stderr);
  return bad ? 1 : 0;
}

i32 hcmain(arena *a, i32 argc, char **argv) {
  heapinit(a);
  // -b, -c FILE, -D DESK, -s FILE, -S FILE and -e HOON, anywhere before the flag
  char *subjects[64];
  i32 exprs[64];
  char *kernel = 0, *desk = 0;
  b32 batch = 0;
  i32 nsubjects = 0, n = 0;
  for (i32 i = 0; i < argc; i++) {
    if (streq(argv[i], "-b")) {
      batch = 1;
    } else if (i + 1 < argc && streq(argv[i], "-D")) {
      desk = argv[++i];
    } else if (i + 1 < argc && streq(argv[i], "-c")) {
      kernel = argv[++i];
    } else if (i + 1 < argc && (streq(argv[i], "-s") || streq(argv[i], "-e") || streq(argv[i], "-S"))
        && nsubjects < countof(subjects)) {
      exprs[nsubjects] = argv[i][1] == 'e' ? 1 : argv[i][1] == 'S' ? 2 : 0;
      subjects[nsubjects++] = argv[++i];
    } else {
      argv[n++] = argv[i];
    }
  }
  argc = n;
  u8 mode = argc > 1 && argv[1][0] == '-' ? (u8)argv[1][1] : 0;
  i32 cap = 1<<12;

  bufout stdout[1] = {0};
  stdout->fd = 1;
  stdout->cap = cap;
  stdout->buf = new(a, u8, cap);

  bufout stderr[1] = {0};
  stderr->fd = 2;
  stderr->cap = cap;
  stderr->buf = new(a, u8, cap);

  // hc SYS PATH..., as people use it
  b32 tests = kernel || desk || batch || nsubjects || mode;
  if (!tests && argc >= 3 && osisdir(argv[1])) return hcuser(a, stdout, stderr, argv[1], argv + 2, argc - 2);
  if (argc < 2 || (mode && !streq(argv[1], "-m") && !streq(argv[1], "-n")
                        && !streq(argv[1], "-k") && !streq(argv[1], "-f")
                        && !streq(argv[1], "-d"))
      || (mode == 'k' && argc < 3) || (mode == 'd' && argc < 4)
      || (!tests && osisdir(argv[1]))) {
    append(stderr, S("usage: hc SYS PATH...\n"
                     "  compile each .hoon file at PATH, or under it if it's a directory,\n"
                     "  as clay would in its desk, against the kernel in SYS, a sys directory\n"
                     "  or a desk with one\n"
                     "for tests: hc [-b] [-c HOON.HOON] [-s FILE | -S FILE | -e HOON]... [-D DESK]\n"
                     "              FILE | -m | -n | -f | -k KERNEL | -d AXIS DEPTH\n"));
    flush(stderr);
    return 2;
  }

  b32 compact;
  noun sut = chainmake(a, stderr, kernel, subjects, exprs, nsubjects, 0, &compact);
  if (!sut) {
    flush(stderr);
    return 2;
  }
  if (mode == 'k') {
    s8 ksrc;
    if (!osreadfile(a, argv[2], &ksrc)) {
      append(stderr, S("cannot read kernel\n"));
      flush(stderr);
      return 2;
    }
    parser kp = newparser(a, ksrc);
    size kpos = 0;
    noun kgen = vest(&kp, &kpos);
    typer ku = {0};
    ku.p = &kp;
    ku.fan = 0;
    sut = kgen ? play(&ku, sut, kgen) : 0;
    if (!sut) {
      append(stderr, S("cannot type kernel\n"));
      flush(stderr);
      return 2;
    }
  }

  if (desk) {
    // desk files through ford, named on the command line or, with -b,
    // a line each on stdin
    fordenv f = {0};
    f.desk = desk;
    f.zuse = sut;
    if (!batch) return compiledesk(a, stdout, stderr, mode, &f, argv[argc-1],
                                   (size)__builtin_strlen(argv[argc-1]), argv);
    s8 in = readstdin(a);
    nouns ls = {0};   // offsets of lines, start and end
    for (size at = 0; at < in.len; ) {
      size end = at;
      while (end < in.len && in.buf[end] != '\n') end++;
      if (end > at) {
        *push(&ls, a) = (noun)at;
        *push(&ls, a) = (noun)end;
      }
      at = end + 1;
    }
    char **rels = new(a, char *, ls.len / 2 + 1);
    for (size i = 0; i < ls.len; i += 2) {
      size n = ls.data[i+1] - ls.data[i];
      rels[i/2] = new(a, char, n + 1);
      copy(rels[i/2], (byte*)in.buf + ls.data[i], n);
    }
    i32 bad = 0;
    deskfiles(a, stdout, stderr, mode, &f, rels, ls.len / 2, compact, &bad);
    return bad ? 1 : 0;
  }
  s8 in;
  if (mode) {
    in = readstdin(a);
  } else if (!osreadfile(a, argv[1], &in)) {
    append(stderr, S("cannot read "));
    append(stderr, (s8){(u8*)argv[1], (size)__builtin_strlen(argv[1])});
    append(stderr, S("\n"));
    flush(stderr);
    return 2;
  }
  if (!batch) return compileone(a, stdout, stderr, mode, sut, in, argv, 0, 0, mode ? "-" : argv[1]);
  // cases separated by lines of %%%, as test/cmp.py has them, a line out
  // for each
  i32 r = 0;
  size at = 0;
  while (at <= in.len) {
    size end = at;
    while (end < in.len && !(in.buf[end] == '\n' && end + 3 < in.len + 0 && in.buf[end+1] == '%'
                             && in.buf[end+2] == '%' && in.buf[end+3] == '%')) {
      end++;
    }
    s8 c = {in.buf + at, end - at};
    if (c.len && c.buf[0] == '\n') {
      c.buf++;
      c.len--;
    }
    b32 blank = 1;
    for (size i = 0; i < c.len; i++) blank &= c.buf[i] == ' ' || c.buf[i] == '\n';
    if (!blank) r |= compileone(a, stdout, stderr, mode, sut, c, argv, 0, 0, "-");
    at = end + 4;
  }
  return r;
}

#ifdef _WIN32
void mainCRTStartup(void) {
  byte top;
  stacktop = &top;
  // reserved, not committed until used
  size cap = (size)1 << 36;
  arena a = {0};
  a.beg = VirtualAlloc(0, (usize)cap, 0x2000, 4);
  a.dat = a.beg;
  a.end = a.beg + cap;
  char **argv;
  i32 argc = splitargs(&a, GetCommandLineA(), &argv);
  i32 r = hcmain(&a, argc, argv);
  profreport();
  ExitProcess(r);
}
#else
i32 main(i32 argc, char **argv) {
  byte top;
  stacktop = &top;
  // reserved, not touched until used
  size cap = (size)1 << 36;
  arena a = {0};
  a.beg = osreserve(cap);
  if (!a.beg) oom();
  a.dat = a.beg;
  a.end = a.beg + cap;
  i32 r = hcmain(&a, argc, argv);
  profreport();
  return r;
}
#endif
