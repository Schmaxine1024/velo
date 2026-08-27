#!/bin/sh
# Cross-check the phone's Format.java against the watch's fmt.c.
#
# The two implement the same formatting rules in different languages, and the
# phone deliberately reproduces the C's integer truncation rather than using
# floating point -- otherwise the same ride reads 0.99 mi on the watch and
# 1.00 mi on the phone. This harness compiles the real fmt.c natively against a
# shim for pebble.h, runs both over the same vectors, and diffs them.
#
# Regenerate app/src/test/java/org/lianas/velo/FormatTest.java from c-output.txt
# whenever the vectors change.
set -e
cd "$(dirname "$0")"
ROOT=../..
gcc -I shim -I "$ROOT/watchapp/src/c" -o fmtcheck main.c "$ROOT/watchapp/src/c/fmt.c"
./fmtcheck > c-output.txt
echo "wrote $(wc -l < c-output.txt) vectors to c-output.txt"
