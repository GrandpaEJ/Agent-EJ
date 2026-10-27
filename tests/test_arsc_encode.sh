#!/usr/bin/env bash
# Regression tests for the native resources.arsc encoder (issue #5).
#
# The encoder has to reproduce a resource table byte for byte: the table is
# decoded into the in-memory model and written back out again. Any field that
# the model does not carry would be silently lost, so the byte comparison is
# the strongest check available.
#
# Real APKs are round tripped as well when they are given:
#   ARSC_TEST_APKS="/path/one.apk /path/two.apk" ./tests/test_arsc_encode.sh
set -u

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
AGENTX="${1:-$ROOT/agent-ej}"
W="$(mktemp -d /tmp/opencode/arsctest.XXXXXX)"
trap 'rm -rf "$W"' EXIT
PASS=0; FAIL=0

ok()   { PASS=$((PASS+1)); printf '  ok   - %s\n' "$1"; }
bad()  { FAIL=$((FAIL+1)); printf '  FAIL - %s\n' "$1"; }

echo "== resource table encoder (issue #5) =="

# --- 1. a synthetic table must survive the round trip untouched ------------
FIX="$W/fixture.arsc"
python3 "$ROOT/tests/arsc_build.py" "$FIX" || { echo "  FAIL - cannot build the fixture"; exit 1; }

out="$("$AGENTX" tool arsc_encode path="$FIX" out="$W/out.arsc" 2>&1)"
if echo "$out" | grep -q '"status": "success"'; then
    ok "the fixture is encoded"
else
    bad "the fixture is encoded: $out"
fi

if cmp -s "$FIX" "$W/out.arsc"; then
    ok "the re-encoded table is byte identical"
else
    bad "the re-encoded table is byte identical"
    cmp -l "$FIX" "$W/out.arsc" 2>&1 | head -5 | sed 's/^/         /'
fi

# --- 2. the independent python dump must not change -------------------------
python3 "$ROOT/tests/arsc_dump.py" "$FIX" "$W/a.txt" 2>/dev/null
python3 "$ROOT/tests/arsc_dump.py" "$W/out.arsc" "$W/b.txt" 2>/dev/null
if [ -s "$W/a.txt" ] && diff -q "$W/a.txt" "$W/b.txt" >/dev/null; then
    ok "the decoded dump is unchanged"
else
    bad "the decoded dump is unchanged"
fi

# --- 3. patching a string must show up in the table ------------------------
"$AGENTX" tool arsc_encode path="$FIX" out="$W/patched.arsc" \
    "set_string=0x7f010000=Patched value" >/dev/null 2>&1
python3 "$ROOT/tests/arsc_dump.py" "$W/patched.arsc" "$W/c.txt" 2>/dev/null
if grep -q "Patched value" "$W/c.txt"; then
    ok "a patched string is written back"
else
    bad "a patched string is written back"
fi

# --- 4. patching an integer must show up in the table ----------------------
"$AGENTX" tool arsc_encode path="$FIX" out="$W/patched2.arsc" \
    "set_int=0x7f010002=4711" >/dev/null 2>&1
python3 "$ROOT/tests/arsc_dump.py" "$W/patched2.arsc" "$W/d.txt" 2>/dev/null
if grep -q "value=16 data=4711" "$W/d.txt"; then
    ok "a patched integer is written back"
else
    bad "a patched integer is written back"
fi

# --- 5. the summary tool must describe the table ---------------------------
info="$("$AGENTX" tool arsc_info path="$FIX" 2>&1)"
if echo "$info" | grep -q '"values":3' && echo "$info" | grep -q 'com.example.fixture'; then
    ok "arsc_info describes the table"
else
    bad "arsc_info describes the table: $info"
fi

# --- 6. real APKs: the table must survive the round trip byte for byte ----
if [ -n "${ARSC_TEST_APKS:-}" ]; then
    # the list is newline separated so that paths with spaces keep working
    while IFS= read -r apk; do
        [ -f "$apk" ] || continue
        name="$(basename "$apk" | tr ' ' '_')"
        "$AGENTX" tool arsc_encode path="$apk" out="$W/$name.arsc" >/dev/null 2>&1
        unzip -o -q -p "$apk" resources.arsc > "$W/$name.orig" 2>/dev/null
        if [ ! -s "$W/$name.orig" ]; then
            printf '  skip - %s has no resources.arsc\n' "$name"
            continue
        fi
        if cmp -s "$W/$name.orig" "$W/$name.arsc"; then
            ok "$name round trips byte for byte"
        else
            bad "$name round trips byte for byte"
        fi
    done < <(printf '%s\n' "$ARSC_TEST_APKS")
fi
echo "-- $PASS passed, $FAIL failed --"
[ "$FAIL" -eq 0 ]
