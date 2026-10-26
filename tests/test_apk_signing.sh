#!/usr/bin/env bash
# APK signing regression test (GitHub issue #13).
#
# Covers the certificate/key mismatch defect: the native signer used to embed
# the built-in certificate while signing with the caller's private key, which
# produced an APK whose v2/v3 signature did not verify.
#
# The test is self-contained: it generates a throwaway key pair, builds a tiny
# synthetic APK, signs it and verifies the result with apksigner.  apksigner is
# the authoritative oracle; when it is not installed the verification steps are
# skipped (the negative case is still checked) and the test reports SKIP
# instead of failing.
#
# Usage: tests/test_apk_signing.sh [path-to-agent-x]

set -u

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
AGENTX="${1:-$ROOT/agent-x}"
WORK="$(mktemp -d "${TMPDIR:-/tmp}/agent-x-signtest.XXXXXX")"
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

PASS=0
FAIL=0
SKIP=0

ok()   { PASS=$((PASS+1)); printf '  ok   - %s\n' "$1"; }
bad()  { FAIL=$((FAIL+1)); printf '  FAIL - %s\n' "$1"; }
skip() { SKIP=$((SKIP+1)); printf '  skip - %s\n' "$1"; }

cleanup() { rm -rf "$WORK"; }
trap cleanup EXIT

printf '== APK signing test (issue #13) ==\n'

if [ ! -x "$AGENTX" ]; then
    printf 'agent-x binary not found at %s - run "make release" first\n' "$AGENTX" >&2
    exit 2
fi

# --- key material -----------------------------------------------------------
# The tool's RSA loader parses PKCS#8 ("BEGIN PRIVATE KEY"), not PKCS#1.
openssl genrsa -traditional -out "$WORK/key.pem" 2048 >/dev/null 2>&1 || {
    printf 'openssl genrsa failed\n' >&2; exit 2; }
openssl pkcs8 -topk8 -nocrypt -in "$WORK/key.pem" -out "$WORK/key_p8.pem" >/dev/null 2>&1 || {
    printf 'openssl pkcs8 failed\n' >&2; exit 2; }
openssl req -new -x509 -key "$WORK/key.pem" -subj "/CN=Agent-X Test" \
    -days 3650 -outform DER -out "$WORK/cert.der" >/dev/null 2>&1 || {
    printf 'openssl req failed\n' >&2; exit 2; }

# A second, unrelated key pair: its certificate must be rejected.
openssl genrsa -traditional -out "$WORK/other.pem" 2048 >/dev/null 2>&1
openssl pkcs8 -topk8 -nocrypt -in "$WORK/other.pem" -out "$WORK/other_p8.pem" >/dev/null 2>&1
openssl req -new -x509 -key "$WORK/other.pem" -subj "/CN=Agent-X Other" \
    -days 3650 -outform DER -out "$WORK/other.der" >/dev/null 2>&1

# --- a tiny synthetic APK ---------------------------------------------------
# apksigner needs a real AndroidManifest.xml inside the APK, otherwise it
# aborts with "Missing AndroidManifest.xml".  The manifest is produced by the
# agent-x AXML encoder; minSdk 24 keeps apksigner from demanding a v1 (JAR)
# signature, so the v2/v3 result is unambiguous.
cat > "$WORK/manifest.xml" <<'XML'
<?xml version="1.0" encoding="utf-8"?>
<manifest xmlns:android="http://schemas.android.com/apk/res/android" package="com.agentx.test">
    <uses-sdk android:minSdkVersion="24" android:targetSdkVersion="34" />
    <application android:label="Agent-X Test" />
</manifest>
XML
if ! "$AGENTX" tool axml_assemble src="$WORK/manifest.xml" out="$WORK/AndroidManifest.xml" >/dev/null 2>&1; then
    printf 'could not build AndroidManifest.xml with axml_assemble\n' >&2
    exit 2
fi

build_apk() {
    out="$1"
    rm -rf "$WORK/apk"; mkdir -p "$WORK/apk"
    printf 'hello agent-x\n' > "$WORK/apk/hello.txt"
    cp "$WORK/AndroidManifest.xml" "$WORK/apk/AndroidManifest.xml"
    rm -f "$WORK/base.zip"
    ( cd "$WORK/apk" && zip -q -X "$WORK/base.zip" hello.txt AndroidManifest.xml )
    cp "$WORK/base.zip" "$out"
}

build_apk "$WORK/test.apk"

# --- case 1: matching certificate -> signature must verify ------------------
OUT="$("$AGENTX" tool resign_apk path="$WORK/test.apk" scheme=v2,v3 \
        key="$WORK/key_p8.pem" cert="$WORK/cert.der" 2>/dev/null)"
RC=$?
case "$OUT" in
    *'"status":"success"'*) ok "matching cert: tool reports success" ;;
    *) bad "matching cert: unexpected output: $OUT" ;;
esac
[ $RC -eq 0 ] && ok "matching cert: exit code 0" || bad "matching cert: exit code $RC"

if [ -n "$APKSIGNER" ]; then
    V="$("$APKSIGNER" verify --verbose "$WORK/test.apk.signed" 2>&1)"
    case "$V" in
        *"Verifies"*) ok "matching cert: apksigner verifies" ;;
        *) bad "matching cert: apksigner says: $V" ;;
    esac
    case "$V" in
        *"v2 scheme (APK Signature Scheme v2): true"*) ok "matching cert: v2 scheme verified" ;;
        *) bad "matching cert: v2 scheme not verified: $V" ;;
    esac
    case "$V" in
        *"v3 scheme (APK Signature Scheme v3): true"*) ok "matching cert: v3 scheme verified" ;;
        *) bad "matching cert: v3 scheme not verified: $V" ;;
    esac
else
    skip "apksigner not installed - signature verification not checked"
fi

# --- case 2: mismatched certificate -> refused, no output file ---------------
OUT="$("$AGENTX" tool resign_apk path="$WORK/test.apk" scheme=v2,v3 \
        key="$WORK/key_p8.pem" cert="$WORK/other.der" 2>/dev/null)"
RC=$?
case "$OUT" in
    *cert_key_mismatch*) ok "mismatched cert: rejected with cert_key_mismatch" ;;
    *) bad "mismatched cert: unexpected output: $OUT" ;;
esac
[ $RC -ne 0 ] && ok "mismatched cert: exit code non-zero ($RC)" \
             || bad "mismatched cert: exit code 0 (failure not reported)"

# --- case 3: no certificate supplied -> must not sign with a foreign cert ---
cp "$WORK/base.zip" "$WORK/nocert.apk"
OUT="$("$AGENTX" tool resign_apk path="$WORK/nocert.apk" scheme=v2,v3 \
        key="$WORK/key_p8.pem" 2>/dev/null)"
RC=$?
case "$OUT" in
    *cert_key_mismatch*) ok "no cert: signing refused (cert_key_mismatch)" ;;
    *'"status":"success"'*) bad "no cert: signed anyway with a mismatched cert" ;;
    *) bad "no cert: unexpected output: $OUT" ;;
esac
[ $RC -ne 0 ] && ok "no cert: exit code non-zero ($RC)" \
             || bad "no cert: exit code 0 (failure not reported)"
[ ! -f "$WORK/nocert.apk.signed" ] && ok "no cert: no half-written output left behind" \
                                  || bad "no cert: half-written output file exists"

# --- case 4: mismatched certificate on the v1 path too -----------------------
cp "$WORK/base.zip" "$WORK/v1.apk"
OUT="$("$AGENTX" tool resign_apk path="$WORK/v1.apk" scheme=v1 \
        key="$WORK/key_p8.pem" cert="$WORK/other.der" 2>/dev/null)"
RC=$?
case "$OUT" in
    *cert_key_mismatch*) ok "v1 path: mismatched cert rejected" ;;
    *) bad "v1 path: unexpected output: $OUT" ;;
esac
[ $RC -ne 0 ] && ok "v1 path: exit code non-zero ($RC)" \
             || bad "v1 path: exit code 0 (failure not reported)"

printf '== pass=%d fail=%d skip=%d ==\n' "$PASS" "$FAIL" "$SKIP"
[ "$FAIL" -eq 0 ] || exit 1
exit 0
