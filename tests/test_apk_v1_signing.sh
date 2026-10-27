#!/usr/bin/env bash
# APK v1 (JAR) signing regression test (GitHub issue #30).
#
# Covers the four acceptance criteria of #30 and the defects fixed along
# the way:
#   1. digest selection mirrors apksigner: SHA-256 for minSdk >= 18
#      (jarsigner-verifiable, no warnings), SHA-1 below (SHA-256 fails
#      apksigner verify on API 15-17), boundary at 17/18
#   2. META-INF content files are preserved and digest-listed (both
#      reference signers keep them; the signer used to drop them)
#   3. long entry names are wrapped at the JAR-spec 72 content bytes
#      (unwrapped lines were rejected by BOTH reference verifiers)
#   4. the PKCS#7 encryptedDigest matches the key modulus (1024-bit keys
#      produced a 256-byte digest apksigner rejects)
#   5. combined scheme=v1,v2,v3 verifies with both apksigner and jarsigner
#
# oracles: apksigner and jarsigner are skipped when absent.  The SHA-1
# fixture is asserted with apksigner only — the JDK disables SHA-1, so
# jarsigner warnings there are policy, not a defect (proven against real
# jarsigner output in issue #30).
#
# Usage: tests/test_apk_v1_signing.sh [path-to-agent-x]

set -u

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
AGENTX="${1:-$ROOT/agent-x}"
WORK="$(mktemp -d "${TMPDIR:-/tmp}/agent-x-v1test.XXXXXX")"
if [ -n "${APKSIGNER:-}" ]; then
    # Explicitly requested: honour it, otherwise there is no oracle available.
    [ -x "$APKSIGNER" ] || APKSIGNER=""
else
    for cand in \
        "$ANDROID_HOME/build-tools/35.0.0/apksigner" \
        "$ANDROID_SDK_ROOT/build-tools/35.0.0/apksigner" \
        "$HOME/Android/Sdk/build-tools/35.0.0/apksigner"
    do
        [ -n "$cand" ] && [ -x "$cand" ] && { APKSIGNER="$cand"; break; }
    done
fi
if [ -n "${JARSIGNER:-}" ]; then
    [ -x "$JARSIGNER" ] || JARSIGNER=""
else
    JARSIGNER="$(command -v jarsigner 2>/dev/null || true)"
fi

PASS=0
FAIL=0
SKIP=0

ok()   { PASS=$((PASS+1)); printf '  ok   - %s\n' "$1"; }
bad()  { FAIL=$((FAIL+1)); printf '  FAIL - %s\n' "$1"; }
skip() { SKIP=$((SKIP+1)); printf '  skip - %s\n' "$1"; }

cleanup() { rm -rf "$WORK"; }
trap cleanup EXIT

printf '== APK v1 signing test (issue #30) ==\n'

if [ ! -x "$AGENTX" ]; then
    printf 'agent-x binary not found at %s - run "make release" first\n' "$AGENTX" >&2
    exit 2
fi
for tool in zip unzip openssl; do
    command -v "$tool" >/dev/null || { printf '%s not found\n' "$tool" >&2; exit 2; }
done

# --- key material -----------------------------------------------------------
# The tool's RSA loader parses PKCS#8 ("BEGIN PRIVATE KEY"), not PKCS#1.
openssl genrsa -traditional -out "$WORK/key.pem" 2048 >/dev/null 2>&1 || {
    printf 'openssl genrsa failed\n' >&2; exit 2; }
openssl pkcs8 -topk8 -nocrypt -in "$WORK/key.pem" -out "$WORK/key_p8.pem" >/dev/null 2>&1
openssl req -new -x509 -key "$WORK/key.pem" -subj "/CN=Agent-X V1 Test" \
    -days 3650 -outform DER -out "$WORK/cert.der" >/dev/null 2>&1

# A 1024-bit key exposes the encryptedDigest sizing bug.
openssl genrsa -traditional -out "$WORK/key1024.pem" 1024 >/dev/null 2>&1
openssl pkcs8 -topk8 -nocrypt -in "$WORK/key1024.pem" -out "$WORK/key1024_p8.pem" >/dev/null 2>&1
openssl req -new -x509 -key "$WORK/key1024.pem" -subj "/CN=Agent-X V1 Test 1024" \
    -days 3650 -outform DER -out "$WORK/cert1024.der" >/dev/null 2>&1

# --- fixtures ---------------------------------------------------------------
# Three AXML manifests: modern (SHA-256 path + apksigner's v1-only
# acceptance needs targetSdk < 24), the 17/18 digest boundary, and a
# long-name APK.
make_fixture() { # dir, package, uses-sdk line
    rm -rf "$1"; mkdir -p "$1"
    cat > "$1.xml" <<XML
<?xml version="1.0" encoding="utf-8"?>
<manifest xmlns:android="http://schemas.android.com/apk/res/android" package="$2">
$3
    <application android:label="Agent-X V1 Test" />
</manifest>
XML
    "$AGENTX" tool axml_assemble src="$1.xml" out="$1/AndroidManifest.xml" >/dev/null 2>&1 || {
        printf 'axml_assemble failed for %s\n' "$1" >&2; exit 2; }
    printf 'hello v1\n' > "$1/hello.txt"
}

# modern: minSdk 24 (-> SHA-256), targetSdk 23 (apksigner accepts v1-only)
make_fixture "$WORK/fmodern" com.agentx.v1mod \
    '    <uses-sdk android:minSdkVersion="24" android:targetSdkVersion="23" />'
# META-INF content files: both reference signers keep and sign them
mkdir -p "$WORK/fmodern/META-INF/services"
printf 'k=v\n' > "$WORK/fmodern/META-INF/probe.version"
printf 'spi\n' > "$WORK/fmodern/META-INF/services/probe.Spi"

# boundary: minSdk 17 -> SHA-1 (apksigner's own rule), targetSdk 23
make_fixture "$WORK/fm17" com.agentx.v1m17 \
    '    <uses-sdk android:minSdkVersion="17" android:targetSdkVersion="23" />'

# combined-scheme host: minSdk 18 (-> SHA-256, also the boundary), target 23
make_fixture "$WORK/fm18" com.agentx.v1m18 \
    '    <uses-sdk android:minSdkVersion="18" android:targetSdkVersion="23" />'

# long entry name: 6+200+1+200+1+134 = 542 bytes -> must wrap at 72
make_fixture "$WORK/flong" com.agentx.v1long \
    '    <uses-sdk android:minSdkVersion="24" android:targetSdkVersion="23" />'
A200=$(printf '%.0sa' {1..200}); B200=$(printf '%.0sb' {1..200}); C130=$(printf '%.0sc' {1..130})
mkdir -p "$WORK/flong/assets/$A200/$B200"
printf 'long-name-entry\n' > "$WORK/flong/assets/$A200/$B200/$C130.bin"

zip_apk() { ( cd "$1" && rm -f "$2" && zip -qr "$2" . ); }
zip_apk "$WORK/fmodern" "$WORK/fmodern.apk"
zip_apk "$WORK/fm17"    "$WORK/fm17.apk"
zip_apk "$WORK/fm18"    "$WORK/fm18.apk"
zip_apk "$WORK/flong"   "$WORK/flong.apk"

mf_digest() { unzip -p "$1" META-INF/MANIFEST.MF | grep -m1 -oE 'SHA-256-Digest|SHA1-Digest'; }

sign_v1() { # apk, key, cert[, scheme]
    "$AGENTX" tool resign_apk path="$1" key="$2" cert="$3" scheme="${4:-v1}"
}

# --- case 1: modern fixture, single v1 ---------------------------------------
OUT="$(sign_v1 "$WORK/fmodern.apk" "$WORK/key_p8.pem" "$WORK/cert.der" 2>/dev/null)"
case "$OUT" in
    *'"status":"success"'*) ok "modern: v1 signing succeeds" ;;
    *) bad "modern: unexpected output: $OUT" ;;
esac
SIGNED="$WORK/fmodern.apk.signed"

D="$(mf_digest "$SIGNED")"
[ "$D" = "SHA-256-Digest" ] \
    && ok "modern (minSdk 24): manifest digest is SHA-256-Digest" \
    || bad "modern (minSdk 24): manifest digest is '$D', expected SHA-256-Digest"

# criterion: META-INF content survives and is digest-listed
if unzip -Z1 "$SIGNED" | grep -q '^META-INF/probe\.version$' \
   && unzip -Z1 "$SIGNED" | grep -q '^META-INF/services/probe\.Spi$'; then
    ok "META-INF content files preserved in the output"
else
    bad "META-INF content files dropped from the output: $(unzip -Z1 "$SIGNED" | grep META-INF | tr '\n' ' ')"
fi
if unzip -p "$SIGNED" META-INF/MANIFEST.MF | grep -q '^Name: META-INF/probe\.version' \
   && unzip -p "$SIGNED" META-INF/MANIFEST.MF | grep -q '^Name: META-INF/services/probe\.Spi'; then
    ok "META-INF content files digest-listed in the manifest"
else
    bad "META-INF content files missing from the manifest"
fi
# the signature files themselves must not be listed
if unzip -p "$SIGNED" META-INF/MANIFEST.MF | grep -qE '^Name: META-INF/CERT\.(SF|RSA)'; then
    bad "manifest lists the signer's own signature files"
else
    ok "manifest excludes the signature files themselves"
fi

if [ -n "$APKSIGNER" ]; then
    V="$("$APKSIGNER" verify --verbose "$SIGNED" 2>&1)"; RC=$?
    case "$V" in
        *Verifies*) ok "modern: apksigner verifies" ;;
        *) bad "modern: apksigner says: $(printf '%s' "$V" | head -3 | tr '\n' ' ')" ;;
    esac
    case "$V" in
        *"Verified using v1 scheme (JAR signing): true"*) ok "modern: apksigner v1 scheme true" ;;
        *) bad "modern: apksigner v1 scheme not verified" ;;
    esac
    [ $RC -eq 0 ] || bad "modern: apksigner exit code $RC"
else
    skip "apksigner not installed - apksigner checks skipped"
fi

if [ -n "$JARSIGNER" ]; then
    V="$("$JARSIGNER" -verify -verbose "$SIGNED" 2>&1)"; RC=$?
    case "$V" in
        *"jar verified"*) ok "modern: jarsigner reports jar verified" ;;
        *) bad "modern: jarsigner says: $(printf '%s' "$V" | head -2 | tr '\n' ' ')" ;;
    esac
    printf '%s' "$V" | grep -q 'nonexistent signed entries' \
        && bad "modern: jarsigner reports nonexistent signed entries" \
        || ok "modern: no 'nonexistent signed entries' warning"
    printf '%s' "$V" | grep -q 'treated as unsigned' \
        && bad "modern: jarsigner treats the jar as unsigned" \
        || ok "modern: jarsigner does not treat the jar as unsigned"
    printf '%s' "$V" | grep -q 'Digest algorithm: SHA-256' \
        && ok "modern: jarsigner reports SHA-256 digest" \
        || bad "modern: jarsigner does not report a SHA-256 digest"
    [ $RC -eq 0 ] || bad "modern: jarsigner exit code $RC"
else
    skip "jarsigner not installed - jarsigner checks skipped"
fi

# --- case 2: digest boundary (minSdk 17 -> SHA-1, apksigner's own rule) ------
OUT="$(sign_v1 "$WORK/fm17.apk" "$WORK/key_p8.pem" "$WORK/cert.der" 2>/dev/null)"
case "$OUT" in
    *'"status":"success"'*) ok "boundary: minSdk 17 signs" ;;
    *) bad "boundary: unexpected output: $OUT" ;;
esac
D="$(mf_digest "$WORK/fm17.apk.signed")"
[ "$D" = "SHA1-Digest" ] \
    && ok "boundary (minSdk 17): manifest digest is SHA1-Digest" \
    || bad "boundary (minSdk 17): manifest digest is '$D', expected SHA1-Digest (apksigner's rule: SHA-1 below 18)"

if [ -n "$APKSIGNER" ]; then
    # SHA-1 verifies on every API level — this is the apksigner-parity path.
    if "$APKSIGNER" verify "$WORK/fm17.apk.signed" >/dev/null 2>&1; then
        ok "boundary (SHA-1): apksigner verifies"
    else
        bad "boundary (SHA-1): apksigner rejects the SHA-1 output"
    fi
else
    skip "apksigner not installed - SHA-1 verification skipped"
fi

# --- case 3: combined v1+v2+v3 on the minSdk 18 fixture -----------------------
OUT="$(sign_v1 "$WORK/fm18.apk" "$WORK/key_p8.pem" "$WORK/cert.der" v1,v2,v3 2>/dev/null)"
case "$OUT" in
    *'"status":"success"'*) ok "combined: v1,v2,v3 signing succeeds" ;;
    *) bad "combined: unexpected output: $OUT" ;;
esac
CSIGNED="$WORK/fm18.apk.signed"
if [ -n "$APKSIGNER" ]; then
    V="$("$APKSIGNER" verify --verbose "$CSIGNED" 2>&1)"
    case "$V" in *Verifies*) ok "combined: apksigner verifies" ;;
        *) bad "combined: apksigner says: $(printf '%s' "$V" | head -3 | tr '\n' ' ')" ;; esac
    for s in 1 2 3; do
        case "$V" in
            *"Verified using v$s scheme"*": true"*) ok "combined: apksigner v$s scheme true" ;;
            *) bad "combined: apksigner v$s scheme not verified" ;;
        esac
    done
else
    skip "apksigner not installed - combined checks skipped"
fi
if [ -n "$JARSIGNER" ]; then
    V="$("$JARSIGNER" -verify "$CSIGNED" 2>&1)"
    case "$V" in
        *"jar verified"*) ok "combined: jarsigner reports jar verified" ;;
        *) bad "combined: jarsigner says: $(printf '%s' "$V" | head -2 | tr '\n' ' ')" ;;
    esac
    printf '%s' "$V" | grep -q 'nonexistent signed entries' \
        && bad "combined: jarsigner reports nonexistent signed entries" \
        || ok "combined: no 'nonexistent signed entries' warning"
else
    skip "jarsigner not installed - combined jarsigner checks skipped"
fi

# --- case 4: long entry name wrapped at the JAR-spec 72 bytes ----------------
OUT="$(sign_v1 "$WORK/flong.apk" "$WORK/key_p8.pem" "$WORK/cert.der" 2>/dev/null)"
case "$OUT" in
    *'"status":"success"'*) ok "long name: v1 signing succeeds" ;;
    *) bad "long name: unexpected output: $OUT" ;;
esac
LSIGNED="$WORK/flong.apk.signed"
MAXLINE="$(unzip -p "$LSIGNED" META-INF/MANIFEST.MF | tr -d '\r' | awk '{ if (length($0) > m) m = length($0) } END { print m+0 }')"
if [ "$MAXLINE" -le 72 ]; then
    ok "long name: manifest lines wrapped at $MAXLINE <= 72 bytes"
else
    bad "long name: manifest line of $MAXLINE bytes exceeds the 72-byte JAR limit"
fi
# the wrapped name must still reconstruct to the full entry name
if unzip -p "$LSIGNED" META-INF/MANIFEST.MF | tr -d '\r\n ' | grep -q "a\{200\}/b\{200\}/c\{130\}\.bin"; then
    ok "long name: full entry name present across continuation lines"
else
    bad "long name: entry name truncated or lost in the manifest"
fi
if [ -n "$JARSIGNER" ]; then
    V="$("$JARSIGNER" -verify "$LSIGNED" 2>&1)"
    case "$V" in
        *"jar verified"*) ok "long name: jarsigner reports jar verified" ;;
        *) bad "long name: jarsigner says: $(printf '%s' "$V" | head -2 | tr '\n' ' ')" ;;
    esac
else
    skip "jarsigner not installed - long-name jarsigner check skipped"
fi
if [ -n "$APKSIGNER" ]; then
    "$APKSIGNER" verify "$LSIGNED" >/dev/null 2>&1 \
        && ok "long name: apksigner verifies" \
        || bad "long name: apksigner rejects the wrapped manifest"
else
    skip "apksigner not installed - long-name apksigner check skipped"
fi

# --- case 5: 1024-bit key, PKCS#7 signature length ---------------------------
OUT="$(sign_v1 "$WORK/fmodern.apk" "$WORK/key1024_p8.pem" "$WORK/cert1024.der" 2>/dev/null)"
case "$OUT" in
    *'"status":"success"'*) ok "1024-bit: v1 signing succeeds" ;;
    *) bad "1024-bit: unexpected output: $OUT" ;;
esac
KSIGNED="$WORK/fmodern.apk.signed"
if [ -n "$APKSIGNER" ]; then
    # the broken fixed-256 embed was rejected with "Bad signature length:
    # got 256 but was expecting 128"
    if "$APKSIGNER" verify "$KSIGNED" >/dev/null 2>&1; then
        ok "1024-bit: apksigner verifies the128-byte signature"
    else
        bad "1024-bit: apksigner rejects the PKCS#7 block"
    fi
else
    skip "apksigner not installed - 1024-bit check skipped"
fi

printf '== pass=%d fail=%d skip=%d ==\n' "$PASS" "$FAIL" "$SKIP"
[ "$FAIL" -eq 0 ] || exit 1
exit 0
