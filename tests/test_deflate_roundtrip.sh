#!/bin/sh
# Deflate encoder: our inflater and zlib must both get the input back.
set -e
cd "$(dirname "$0")/.."
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT
cc -O2 -Wall -Iinclude -Isrc/android/smali -Isrc/android/arsc -o "$T/rt" tests/test_deflate_roundtrip.c src/formats/zip/format_zip_deflate.c \
   src/formats/zip/format_zip_huff.c src/formats/zip/format_zip_inflate.c
: > "$T/empty"
printf a > "$T/one"
head -c 300000 /dev/zero > "$T/zeros"
head -c 200000 /dev/urandom > "$T/random"
python3 -c "import random; random.seed(7); print(''.join(random.choice('ab') for _ in range(100000)))" > "$T/ab"
cp include/formats.h "$T/text"
for f in "$@"; do cp "$f" "$T/$(basename "$f")"; done
"$T/rt" "$T"/empty "$T"/one "$T"/zeros "$T"/random "$T"/ab "$T"/text $(for f in "$@"; do echo "$T/$(basename "$f")"; done)
python3 - "$T" <<'PY'
import sys, os, zlib
d = sys.argv[1]; bad = 0
for n in os.listdir(d):
    if not n.endswith('.dfl'): continue
    src = open(os.path.join(d, n[:-4]), 'rb').read()
    raw = open(os.path.join(d, n), 'rb').read()
    try: ok = zlib.decompress(raw, -15) == src
    except Exception as e: ok = False
    ref = len(zlib.compress(src, 9)) - 6
    print(('zlib ok  ' if ok else 'zlib FAIL'), n[:-4], 'ours', len(raw), 'zlib-9', ref)
    bad += not ok
sys.exit(bad)
PY
