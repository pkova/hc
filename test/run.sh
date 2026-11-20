#!/bin/sh
# Build the parser and compare it against the reference on all cases.
# Needs test/urbit135, see mkoracle.py, and test/urbit, see pin.sh.
cd "$(dirname "$0")"
clang ../main.c -g3 -Wall -Wextra -Wconversion -Wdouble-promotion -Wno-unused-parameter \
  -Wno-unused-function -Wno-sign-conversion -fsanitize=undefined -fsanitize-trap -o hp || exit 1
clang -O2 ../main.c -o hp-fast || exit 1
KERNEL=${KERNEL:-urbit/pkg/arvo/sys/hoon.hoon}

check() {
  printf '%s: ' "$*"
  ./cmp.py "$@" | tail -1
}

# parsing, as +ream
for f in cases[0-9]*.txt; do check -m x "$f"; done
# desugaring, as +open, and spec expansion
check -m o cases_open.txt
check -m o cases8.txt
# types against %noun and against the kernel, as +play
check -m t cases_play.txt
check -m k -k "$KERNEL" cases_kernel.txt
# go to definition, in the pinned urbit and test/desk
printf 'gotodef.txt: '
./gdcheck.py | tail -1
# the language server, and that its answers match the command line
printf 'lsptest.py: '
./lsptest.py | tail -1
A=urbit/pkg/arvo
printf 'lspscan.py: '
./lspscan.py -n 6 $A/sys/vane/behn.hoon $A/app/dojo.hoon $A/lib/strandio.hoon $A/mar/json.hoon \
  desk/app/marks.hoon desk/app/broken.hoon | tail -1
