#!/usr/bin/env bash
# Editing colours, dimensions, booleans and integers through arsc_tr: values
# set here must read back the same in aapt2, bad text must be refused, and a
# missing language must be created. Needs the SDK's aapt2 (ANDROID_HOME).
set -u
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
AAPT2="$(ls "${ANDROID_HOME:-$HOME/Android/Sdk}"/build-tools/*/aapt2 2>/dev/null | tail -1)"
[ -x "$AAPT2" ] || { echo "skip: aapt2 not found"; exit 0; }
W="$(mktemp -d)"; trap 'rm -rf "$W"' EXIT
PASS=0; FAIL=0
check() { if grep -qF -- "$2" "$W/dump.txt"; then PASS=$((PASS+1)); echo "  ok   - $1"; else FAIL=$((FAIL+1)); echo "  FAIL - $1 (want: $2)"; fi; }

mkdir -p "$W/res/values"
cat > "$W/res/values/v.xml" <<'X'
<resources>
  <color name="accent">#2196f3</color>
  <color name="scrim">#80000000</color>
  <dimen name="pad">16dp</dimen>
  <dimen name="text">14sp</dimen>
  <bool name="flag">false</bool>
  <integer name="count">3</integer>
</resources>
X
printf '<manifest xmlns:android="http://schemas.android.com/apk/res/android" package="t.v"/>' > "$W/AndroidManifest.xml"
"$AAPT2" compile --dir "$W/res" -o "$W/res.zip" >/dev/null &&
"$AAPT2" link -o "$W/in.apk" --manifest "$W/AndroidManifest.xml" -I "$(ls "${ANDROID_HOME:-$HOME/Android/Sdk}"/platforms/*/android.jar | tail -1)" "$W/res.zip" >/dev/null || { echo "FAIL - cannot build fixture"; exit 1; }
(cd "$W" && unzip -qo in.apk resources.arsc)

OBJS=$(find "$ROOT/obj" -name '*.o' ! -name main.o)
zig cc -target x86_64-linux-musl -flto -I"$ROOT/include" -I"$ROOT/src/android/smali" -I"$ROOT/src/android/arsc" \
    -o "$W/tv" "$ROOT/tests/test_arsc_values.c" $OBJS 2>/dev/null || { echo "FAIL - build (run make first)"; exit 1; }

id() { "$W/tv" "$W/resources.arsc" "$1" "$W/x.arsc" | awk -F'\t' -v k="$2" '$2 == k { print $1 }'; }
"$W/tv" "$W/resources.arsc" color "$W/x.arsc" | grep -q $'\taccent\t#2196f3' && PASS=$((PASS+1)) && echo "  ok   - dump colour" || { FAIL=$((FAIL+1)); echo "  FAIL - dump colour"; }
"$W/tv" "$W/resources.arsc" dimen "$W/x.arsc" | grep -q $'\tpad\t16dp' && PASS=$((PASS+1)) && echo "  ok   - dump dimen" || { FAIL=$((FAIL+1)); echo "  FAIL - dump dimen"; }
OUT=$("$W/tv" "$W/resources.arsc" color \
    "$(id color accent)" "" "#f00" \
    "$(id color scrim)" "" "#40112233" \
    "$(id dimen pad)" "" "1.5dp" \
    "$(id dimen text)" "" "18sp" \
    "$(id bool flag)" "" "true" \
    "$(id integer count)" "" "0x10" \
    "$(id integer count)" "-fr" "7" \
    "$(id dimen pad)" "" "12furlongs" \
    "$(id bool flag)" "" "maybe" \
    "$W/out.arsc")
echo "$OUT" | grep -q "'12furlongs' -> -1" && echo "$OUT" | grep -q "'maybe' -> -1" && PASS=$((PASS+1)) && echo "  ok   - bad text refused" || { FAIL=$((FAIL+1)); echo "  FAIL - bad text refused"; }
cp "$W/in.apk" "$W/out.apk" && (cd "$W" && cp out.arsc resources.arsc && zip -q -0 out.apk resources.arsc)
"$AAPT2" dump resources "$W/out.apk" > "$W/dump.txt" 2>&1
check "colour #rgb expands" "() #ffff0000"
check "colour with alpha" "() #40112233"
check "fractional dimen" "() 1.500000dp"
check "sp dimen" "() 18.000000sp"
check "bool" "() true"
check "hex integer" "() 0x00000010"
check "new language" "(fr) 7"
echo "-- $PASS passed, $FAIL failed --"
[ "$FAIL" -eq 0 ]
