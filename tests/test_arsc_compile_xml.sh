#!/usr/bin/env bash
# Regression tests for the XML resource compiler (XML -> resources.arsc).
#
# decode_apk turns resources.arsc into a res values tree of XML files, but
# build_apk used to pack whatever resources.arsc was on disk, so an edit made
# in one of those files never reached the rebuilt APK.  arsc_compile_xml()
# closes that loop.
#
# The strongest check here is the unedited round trip: entries the tree does
# not describe must keep their value, so compiling a tree that was just decoded
# has to reproduce the original table byte for byte.  Anything the compiler got
# wrong — a configuration it matched too eagerly, a type it rewrote, a string it
# appended twice — changes those bytes.
#
# Real APKs are exercised as well when they are given:
#   ARSC_TEST_APKS="/path/one.apk /path/two.apk" ./tests/test_arsc_compile_xml.sh
#
# Usage: tests/test_arsc_compile_xml.sh [path-to-agent-x]
set -u

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
AGENTX="${1:-$ROOT/agent-x}"
W="$(mktemp -d /tmp/opencode/arscxml.XXXXXX)"
trap 'rm -rf "$W"' EXIT
PASS=0; FAIL=0

ok()   { PASS=$((PASS+1)); printf '  ok   - %s\n' "$1"; }
bad()  { FAIL=$((FAIL+1)); printf '  FAIL - %s\n' "$1"; }

echo "== resource table compiler (XML -> resources.arsc) =="

for tool in python3 unzip cmp; do
    command -v "$tool" >/dev/null || { echo "$tool not found"; exit 2; }
done
[ -x "$AGENTX" ] || { echo "agent-x not found at $AGENTX - run make release first"; exit 2; }

# --- the fixture and the res tree that describes it -------------------------
FIX="$W/fixture.arsc"
python3 "$ROOT/tests/arsc_build.py" "$FIX" || { echo "  FAIL - cannot build the fixture"; exit 1; }
cp "$FIX" "$W/fixture.orig"

# Mirrors the fixture: type "string" holds greeting, title and night in the
# default configuration, where night is the integer 42.
mkdir -p "$W/tree/res/values" "$W/tree/res/values-night"
cat > "$W/tree/res/values/strings.xml" <<'XML'
<?xml version="1.0" encoding="utf-8"?>
<resources>
  <string name="greeting">Hello Agent</string>
  <string name="title">Second string</string>
  <integer name="night">42</integer>
</resources>
XML
cat > "$W/tree/res/values-night/strings.xml" <<'XML'
<?xml version="1.0" encoding="utf-8"?>
<resources>
  <string name="greeting">Night value</string>
</resources>
XML
# public.xml records ids only; the compiler must ignore it.
cat > "$W/tree/res/values/public.xml" <<'XML'
<?xml version="1.0" encoding="utf-8"?>
<resources>
  <public type="string" name="greeting" id="0x7f010000" />
</resources>
XML
# A bag and an unknown type: neither may disturb the table.
cat > "$W/tree/res/values/styles.xml" <<'XML'
<?xml version="1.0" encoding="utf-8"?>
<resources>
  <style name="Theme.Fixture" parent="@android:style/Theme">
    <item name="android:color">#ff000000</item>
  </style>
</resources>
XML

compile() { # arsc, res_dir, out
    "$AGENTX" tool arsc_compile arsc="$1" res_dir="$2" out="$3"
}

# --- 1. an unedited tree must reproduce the table byte for byte -------------
OUT="$(compile "$FIX" "$W/tree/res" "$W/out.arsc" 2>&1)"
case "$OUT" in
    *'"status": "success"'*) ok "the tool compiles the tree" ;;
    *) bad "the tool compiles the tree: $OUT" ;;
esac
if cmp -s "$W/fixture.orig" "$W/out.arsc"; then
    ok "an unedited tree re-encodes the table byte for byte"
else
    bad "an unedited tree re-encodes the table byte for byte"
    cmp -l "$W/fixture.orig" "$W/out.arsc" 2>&1 | head -3 | sed 's/^/         /'
fi
case "$OUT" in
    *'"changed": 0'*) ok "an unedited tree changes no entry" ;;
    *) bad "an unedited tree changes no entry: $OUT" ;;
esac

# --- 2. a string edit must land in the default configuration ----------------
cp -r "$W/tree" "$W/edit"
python3 - "$W/edit/res/values/strings.xml" <<'PY'
import sys
p = sys.argv[1]
s = open(p, encoding='utf-8').read()
s = s.replace('<string name="greeting">Hello Agent</string>',
              '<string name="greeting">COMPILED_OK</string>', 1)
open(p, 'w', encoding='utf-8').write(s)
PY
OUT="$(compile "$FIX" "$W/edit/res" "$W/edited.arsc" 2>&1)"
case "$OUT" in
    *'"changed": 1'*) ok "a single edit is reported as one change" ;;
    *) bad "a single edit is reported as one change: $OUT" ;;
esac
python3 - "$W/edited.arsc" <<'PY'
import sys
data = open(sys.argv[1], 'rb').read()
sys.exit(0 if b'COMPILED_OK' in data else 1)
PY
[ $? -eq 0 ] && ok "the edited string is written into the table" \
             || bad "the edited string is written into the table"

# The other two entries of the fixture must be untouched: only the pool grows
# and the edited entry moves to its new string.
python3 "$ROOT/tests/arsc_dump.py" "$W/fixture.orig" "$W/a.txt" 2>/dev/null
python3 "$ROOT/tests/arsc_dump.py" "$W/edited.arsc" "$W/b.txt" 2>/dev/null
t_before="$(grep "key='title'" "$W/a.txt")";   t_after="$(grep "key='title'" "$W/b.txt")"
n_before="$(grep "key='night'" "$W/a.txt")";   n_after="$(grep "key='night'" "$W/b.txt")"
g_before="$(grep "key='greeting'" "$W/a.txt")"; g_after="$(grep "key='greeting'" "$W/b.txt")"
if [ -n "$t_before" ] && [ "$t_before" = "$t_after" ] \
   && [ -n "$n_before" ] && [ "$n_before" = "$n_after" ] \
   && [ -n "$g_before" ] && [ "$g_before" != "$g_after" ]; then
    ok "the untouched entries keep their value, only the edit moves"
else
    bad "the untouched entries keep their value, only the edit moves"
fi

# --- 3. an unknown type and a bag must be skipped, not corrupted ------------
case "$OUT" in
    *'"skipped": '*) ok "skipped values are counted" ;;
    *) bad "skipped values are counted: $OUT" ;;
esac
if grep -q "value=1 " "$W/b.txt" && grep -q "count=1" "$W/b.txt"; then
    ok "the bag entry survives the compile"
else
    # the bag is the complex entry of type 2 in the fixture
    if grep -q "flags=1" "$W/b.txt"; then
        ok "the bag entry survives the compile"
    else
        bad "the bag entry survives the compile"
    fi
fi

# --- 4. build_apk must pack the compiled table ------------------------------
if command -v zip >/dev/null; then
    cp -r "$W/edit" "$W/build"
    cp "$FIX" "$W/build/resources.arsc"
    if "$AGENTX" tool build_apk src_dir="$W/build" out_apk="$W/built.apk" >/dev/null 2>&1 \
       && [ -s "$W/built.apk" ]; then
        ok "build_apk packs the tree"
        unzip -p "$W/built.apk" resources.arsc > "$W/packed.arsc" 2>/dev/null
        if [ -s "$W/packed.arsc" ] && grep -q 'COMPILED_OK' \
             < <(python3 "$ROOT/tests/arsc_dump.py" "$W/packed.arsc" "$W/c.txt" 2>/dev/null; cat "$W/c.txt" 2>/dev/null); then
            ok "build_apk packs the compiled table, not the original"
        else
            bad "build_apk packs the compiled table, not the original"
        fi
    else
        bad "build_apk packs the tree"
    fi
else
    printf '  skip - zip not installed, build wiring check skipped\n'
fi

# --- 5. real APKs: unedited round trip and configuration handling -----------
entries_of() { python3 "$ROOT/tests/arsc_xml_probe.py" entries "$1" "$2"; }

if [ -n "${ARSC_TEST_APKS:-}" ]; then
    A2=""
    for cand in "$ANDROID_HOME/build-tools/35.0.0/aapt2" \
                "$ANDROID_SDK_ROOT/build-tools/35.0.0/aapt2" \
                "$HOME/Android/Sdk/build-tools/35.0.0/aapt2"; do
        [ -n "$cand" ] && [ -x "$cand" ] && { A2="$cand"; break; }
    done

    while IFS= read -r apk; do
        [ -f "$apk" ] || continue
        name="$(basename "$apk" | tr ' ' '_')"
        rm -rf "$W/d_$name" "$W/q_$name"
        mkdir -p "$W/d_$name"
        "$AGENTX" tool decode_apk path="$apk" out_dir="$W/d_$name" >/dev/null 2>&1
        [ -f "$W/d_$name/resources.arsc" ] || { printf '  skip - %s has no resources.arsc\n' "$name"; continue; }

        if "$AGENTX" tool arsc_compile arsc="$W/d_$name/resources.arsc" \
               res_dir="$W/d_$name/res" out="$W/r_$name.arsc" >/dev/null 2>&1 \
           && cmp -s "$W/d_$name/resources.arsc" "$W/r_$name.arsc"; then
            ok "$name round trips its unedited tree byte for byte"
        else
            bad "$name round trips its unedited tree byte for byte"
        fi

        if [ -n "$A2" ]; then
            # aapt2 reads a table out of a package and wants a manifest to
            # return success, so the bare table is zipped with one for the
            # check.
            rm -rf "$W/z_$name"; mkdir -p "$W/z_$name"
            cp "$W/r_$name.arsc" "$W/z_$name/resources.arsc"
            if [ -f "$W/d_$name/AndroidManifest.xml" ]; then
                cp "$W/d_$name/AndroidManifest.xml" "$W/z_$name/"
            fi
            if (cd "$W/z_$name" && zip -q "$W/z_$name.zip" resources.arsc AndroidManifest.xml 2>/dev/null) \
               && "$A2" dump resources "$W/z_$name.zip" >/dev/null 2>&1; then
                ok "$name compiles to a table aapt2 accepts"
            else
                bad "$name compiles to a table aapt2 accepts"
            fi
        fi

        # A value in a qualified directory has to reach that configuration and
        # leave the default one alone.  The candidate is any value type that
        # appears in both a values-* directory and the plain values one; its
        # type comes along so the replacement keeps that type.
        candidate="$(python3 - "$W/d_$name/res" <<'PY'
import glob, os, re, sys
res = sys.argv[1]
pat = re.compile(r'<(string|integer|bool|color) name="([^"]+)">([^<]*)</(?:string|integer|bool|color)>')
for d in sorted(glob.glob(os.path.join(res, 'values-*'))):
    if not os.path.isdir(d):
        continue
    for f in sorted(glob.glob(os.path.join(d, '*.xml'))):
        if os.path.basename(f) == 'public.xml':
            continue
        try:
            text = open(f, encoding='utf-8', errors='replace').read()
        except OSError:
            continue
        m = pat.search(text)
        if not m:
            continue
        default = os.path.join(res, 'values', os.path.basename(f))
        if not os.path.exists(default):
            continue
        dtxt = open(default, encoding='utf-8', errors='replace').read()
        if re.search(r'<%s name="%s">' % (m.group(1), re.escape(m.group(2))), dtxt):
            print('%s %s %s' % (f, m.group(1), m.group(2)))
            sys.exit(0)
sys.exit(0)
PY
)"
        if [ -n "$candidate" ]; then
            qf="$(printf '%s' "$candidate" | cut -d' ' -f1)"
            qtype="$(printf '%s' "$candidate" | cut -d' ' -f2)"
            qkey="$(printf '%s' "$candidate" | cut -d' ' -f3)"
            cp -r "$W/d_$name" "$W/q_$name"
            qpatched="$W/q_$name/${qf#$W/d_$name/}"
            # The replacement has to keep the entry's type: the compiler reads
            # a value back the way the decoder wrote it, so a marker string in
            # an <integer> would (correctly) be refused rather than stored.
            case "$qtype" in
                integer) qvalue=777 ;;
                bool)    qvalue=false ;;
                color)   qvalue='#ff112233' ;;
                *)       qvalue=QUALIFIED_EDIT ;;
            esac
            python3 - "$qpatched" "$qkey" "$qtype" "$qvalue" <<'PY'
import re, sys
p, key, tag, value = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4]
s = open(p, encoding='utf-8').read()
# Two groups: the opening tag and the closing one.
s2 = re.sub(r'(<%s name="%s">)[^<]*(</%s>)' % (tag, re.escape(key), tag),
            lambda m: m.group(1) + value + m.group(2), s, count=1)
if s2 == s:
    sys.exit(1)
open(p, 'w', encoding='utf-8').write(s2)
PY
            before="$(entries_of "$W/d_$name/resources.arsc" "$qkey")"
            if "$AGENTX" tool arsc_compile arsc="$W/d_$name/resources.arsc" \
                   res_dir="$W/q_$name/res" out="$W/q_$name.arsc" >/dev/null 2>&1; then
                after="$(entries_of "$W/q_$name.arsc" "$qkey")"
                # First line is the default configuration; it must not move.
                if [ -n "$before" ] && [ "$before" != "$after" ] \
                   && [ "$(printf '%s' "$before" | head -1)" = "$(printf '%s' "$after" | head -1)" ]; then
                    ok "$name edits a qualified configuration and leaves the default one"
                else
                    bad "$name edits a qualified configuration and leaves the default one"
                fi
            else
                bad "$name edits a qualified configuration and leaves the default one"
            fi
        else
            printf '  skip - %s has no value shared between a qualified and the default directory\n' "$name"
        fi
    done < <(printf '%s\n' "$ARSC_TEST_APKS")
fi

echo "-- $PASS passed, $FAIL failed --"
[ "$FAIL" -eq 0 ]
