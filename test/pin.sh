#!/bin/sh
# Make test/urbit, the kernel and base-dev the tests are written against:
# pkg/arvo and pkg/base-dev of the urbit repository at a pinned commit,
# copied out so that edits and checkouts there don't move the lines the
# tests name.
#
#   ./pin.sh [REPO]     REPO defaults to ~/Desktop/urbit
#
# To move the pin, change COMMIT and fix the lines in gotodef.txt.

set -e
cd "$(dirname "$0")"
COMMIT=d6ec15fbcb5675af6ac722e6f512f399a5fdbaef
REPO=${1:-$HOME/Desktop/urbit}

rm -rf urbit
mkdir urbit
git -C "$REPO" archive "$COMMIT" pkg/arvo pkg/base-dev | tar -x -C urbit
echo "$COMMIT" > urbit/COMMIT
echo "pinned test/urbit at $COMMIT"
