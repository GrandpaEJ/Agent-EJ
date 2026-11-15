#!/usr/bin/env bash
# Syntax-error diagnostics for the smali assembler (issue #8).
# Every case must be rejected with a "file:line: message" diagnostic.
set -u
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
AGENTX="${1:-$ROOT/agent-ej}"
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

# issue #37: an array literal on a scalar field used to encode as int 0
expect_fail array_on_scalar \
  "$(mk array_on_scalar '.field public static n:I = {0x1, 0x2}')" \
  "array initializer on non-array field n:I"

# p0 of an instance method is "this", which has no debug name slot
expect_fail param_this \
  "$(mk param_this '.method public m(I)V' '    .registers 2' '    .param p0, "x"' '    return-void' '.end method')" \
  ".param p0 is not a parameter of m(I)V"

# the second register of a wide parameter is not a parameter of its own
expect_fail param_wide_half \
  "$(mk param_wide_half '.method public static m(J)V' '    .registers 2' '    .param p1, "x"' '    return-void' '.end method')" \
  ".param p1 is not a parameter of m(J)V"

# the diagnostic must carry the file name and the line of the defect
expect_fail line_number \
  "$(mk line_number '.method public static m()V' '    .registers 1' '    return-void' '    frobnicate' '.end method')" \
  "T.smali:6:"

# register-range violations found at encode time name file:line too (#35)
expect_fail validate_line \
  "$(mk validate_line '.method public static m()V' '    .registers 17' '    const/4 v0, 0x0' '    move v16, v0' '    return-void' '.end method')" \
  "T.smali:6: register vA out of range for F_12x (v16 > v15)"

# recovery: every violation in a method is reported, not just the first
d="$(mk validate_many '.method public static m()V' '    .registers 300' '    move v16, v0' '    const-string v256, "a"' '    return-void' '.end method')"
expect_fail validate_many_1 "$d" "T.smali:5: register vA out of range for F_12x"
expect_fail validate_many_2 "$d" "T.smali:6: register vA out of range for F_21c"

# recovery: a broken method doesn't hide errors in the methods after it
d="$(mk recover_methods '.method public static a()V' '    .registers 1' '    frobnicate' '.end method' \
      '.method public static b()V' '    .registers 1' '    return-void' '.end method' \
      '.method public static c()V' '    .registers 1' '    move-wid v0, v0' '.end method')"
expect_fail recover_methods_1 "$d" "T.smali:5: unknown instruction 'frobnicate'"
expect_fail recover_methods_2 "$d" "T.smali:13: unknown instruction 'move-wid'"

# recovery across files: each broken file reports its own name
d="$(mk recover_files '.method public static a()V' '    .registers 1' '    frobnicate' '.end method')"
{ echo '.class public Lcom/x/U;'; echo '.super Ljava/lang/Object;'; echo '.bogus'; } > "$d/com/x/U.smali"
expect_fail recover_files_1 "$d" "T.smali:5: unknown instruction 'frobnicate'"
expect_fail recover_files_2 "$d" "U.smali:3: unknown directive '.bogus'"

# diagnostics are capped so a hopeless file doesn't flood the report
args=()
for i in $(seq 1 25); do args+=(".method public static m$i()V" '    .registers 1' '    frobnicate' '.end method'); done
expect_fail diag_cap "$(mk diag_cap "${args[@]}")" "... 5 more error(s) not shown"

printf '== pass=%d fail=%d ==\n' "$PASS" "$FAIL"
[ "$FAIL" -eq 0 ] || exit 1
exit 0
