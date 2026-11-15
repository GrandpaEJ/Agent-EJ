#!/usr/bin/env bash
# Builds tests/test_smali_pool_check.c against the assembler objects and runs
# it (issue #38). Run `make` first so obj/ is current.
set -eu
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
OUT="$(mktemp -d)"
trap 'rm -rf "$OUT"' EXIT
CC="${CC:-zig cc -target x86_64-linux-musl}"
$CC -std=c11 -D_POSIX_C_SOURCE=200809L -Iinclude -Isrc/android/smali -Isrc/android/arsc \
    -o "$OUT/t" tests/test_smali_pool_check.c \
    obj/android/smali/smali_pool.o obj/android/smali/smali_pool_check.o \
    obj/android/smali/smali_error.o obj/android/smali/smali_typelist.o \
    obj/android/smali/smali_free.o obj/android/smali/smali_annot.o obj/android/smali/smali_lexer.o
"$OUT/t"
