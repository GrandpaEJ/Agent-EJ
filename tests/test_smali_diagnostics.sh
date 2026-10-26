#!/usr/bin/env bash
# Syntax-error diagnostics for the smali assembler (issue #8).
# Every case must be rejected with a "file:line: message" diagnostic.
set -u
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
AGENTX="${1:-$ROOT/agent-x}"
W="$(mktemp -d /tmp/opencode/syntest.XXXXXX)"
trap 'rm -rf "$W"' EXIT
PASS=0; FAIL=0

mk() { # mk <name> <body...>
    name="$1"; shift
    d="$W/$name"; mkdir -p "$d/com/x"
    { echo '.class public Lcom/x/T;'
      echo '.super Ljava/lang/Object;'
      for l in "$@"; do echo "$l"; done
    } > "$d/com/x/T.smali"
    echo "$d"
}

expect_fail() { # expect_fail <name> <dir> <expected substring>
    name="$1"; d="$2"; want="$3"
    out="$("$AGENTX" tool smali_assemble src_dir="$d" out_dex="$W/$name.dex" 2>&1)"
    rc=$?
    if [ $rc -eq 0 ]; then
        FAIL=$((FAIL+1)); printf '  FAIL - %s accepted but should be rejected\n' "$name"; return
    fi
    case "$out" in
        *"$want"*) PASS=$((PASS+1)); printf '  ok   - %s: %s\n' "$name" "$(echo "$out" | grep -o '"error":"[^"]*"' | head -1)";;
        *) FAIL=$((FAIL+1)); printf '  FAIL - %s wrong diagnostic (want "%s"): %s\n' "$name" "$want" "$out";;
    esac
}

echo "== syntax diagnostics (issue #8) =="

expect_fail unknown_insn \
  "$(mk unknown_insn '.method public static m()V' '    .registers 1' '    move-wid v0, v1' '    return-void' '.end method')" \
  "unknown instruction 'move-wid'"

expect_fail unknown_directive \
  "$(mk unknown_directive '.method public static m()V' '    .registers 1' '    .frobnicate 3' '    return-void' '.end method')" \
  "unknown directive '.frobnicate'"

expect_fail bad_field \
  "$(mk bad_field '.field public name' '.method public static m()V' '    .registers 1' '    return-void' '.end method')" \
  "malformed .field"

expect_fail bad_method \
  "$(mk bad_method '.method public static m' '.registers 1' '    return-void' '.end method')" \
  "malformed .method"

expect_fail bad_class \
  "$(mk bad_class '.class public' '.method public static m()V' '    .registers 1' '    return-void' '.end method')" \
  "malformed .class"

expect_fail class_directive \
  "$(mk class_directive '.method public static m()V' '    .registers 1' '    return-void' '.end method' '.frobnicate x')" \
  "unknown directive '.frobnicate'"

# the diagnostic must carry the file name and the line of the defect
expect_fail line_number \
  "$(mk line_number '.method public static m()V' '    .registers 1' '    return-void' '    frobnicate' '.end method')" \
  "T.smali:6:"

printf '== pass=%d fail=%d ==\n' "$PASS" "$FAIL"
[ "$FAIL" -eq 0 ] || exit 1
exit 0
