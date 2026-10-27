#!/usr/bin/env bash
# Register-range validation checks for the smali assembler (issue #22).
#
# Each case is a one-method smali tree; the assembler must accept the valid
# ones and reject the invalid ones with a non-zero exit code and a JSON error.
#
# Format limits (DEX bytecode format table) exercised here:
#   F_12x  vA,vB <= 15      F_21c  vA <= 255
#   F_22x  vA <= 255, vB <= 65535        F_51l  vA <= 255
#   F_32x  vA,vB <= 65535   F_35c  count <= 5, arg regs <= 15
#   F_3rc  count <= 255, first reg <= 65535
set -u

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
AGENTX="${1:-$ROOT/agent-ej}"
W="$(mktemp -d /tmp/opencode/valtest.XXXXXX)"
trap 'rm -rf "$W"' EXIT
PASS=0; FAIL=0

mk() { # mk <name> <insn lines...>
    name="$1"; shift
    d="$W/$name"; mkdir -p "$d/com/x"
    { echo '.class public Lcom/x/T;'
      echo '.super Ljava/lang/Object;'
      echo '.method public static m()V'
      echo '    .registers 8'
      for l in "$@"; do echo "$l"; done
      echo '    return-void'
      echo '.end method'
    } > "$d/com/x/T.smali"
    echo "$d"
}

expect_ok() {
    name="$1"; d="$2"
    out="$("$AGENTX" tool smali_assemble src_dir="$d" out_dex="$W/$name.dex" 2>&1)"
    rc=$?
    if [ $rc -eq 0 ]; then PASS=$((PASS+1)); printf '  ok   - %s accepted\n' "$name"
    else FAIL=$((FAIL+1)); printf '  FAIL - %s rejected (rc=%d): %s\n' "$name" "$rc" "$out"; fi
}

expect_fail() {
    name="$1"; d="$2"
    out="$("$AGENTX" tool smali_assemble src_dir="$d" out_dex="$W/$name.dex" 2>&1)"
    rc=$?
    if [ $rc -ne 0 ]; then PASS=$((PASS+1)); printf '  ok   - %s rejected: %s\n' "$name" "$(echo "$out" | grep -o '"error":"[^"]*"' | head -1)"
    else FAIL=$((FAIL+1)); printf '  FAIL - %s accepted but should be rejected\n' "$name"; fi
}

echo "== register validation (issue #22) =="

# --- must be accepted -------------------------------------------------------
expect_ok f12x_max    "$(mk f12x_max    '    move v15, v15')"
expect_ok f21c_max    "$(mk f21c_max    '    const-string v255, \"x\"')"
expect_ok f22x_vb     "$(mk f22x_vb     '    move/from16 v0, v300')"
expect_ok f22x_vb_max "$(mk f22x_vb_max '    move/from16 v255, v65535')"
expect_ok f32x_max    "$(mk f32x_max    '    move/16 v0, v65535')"
expect_ok f32x_va_max "$(mk f32x_va_max '    move/16 v65535, v0')"
expect_ok f51l_max    "$(mk f51l_max    '    const-wide v255, 0x1')"
expect_ok f35c_5      "$(mk f35c_5      '    invoke-static {v0, v1, v2, v3, v4}, Ljava/lang/Object;->hashCode()I')"
expect_ok f3rc_ok     "$(mk f3rc_ok     '    invoke-static/range {v0 .. v4}, Ljava/lang/Object;->hashCode()I')"

# --- must be rejected -------------------------------------------------------
expect_fail f12x_v16    "$(mk f12x_v16    '    move v16, v0')"
expect_fail f21c_v256   "$(mk f21c_v256   '    const-string v256, \"x\"')"
expect_fail f22x_va     "$(mk f22x_va     '    move/from16 v256, v0')"
expect_fail f22x_vb     "$(mk f22x_vb_bad '    move/from16 v0, v65536')"
expect_fail f32x_va     "$(mk f32x_va     '    move/16 v65536, v0')"
expect_fail f32x_vb     "$(mk f32x_vb     '    move/16 v0, v65536')"
expect_fail f51l_v256   "$(mk f51l_v256   '    const-wide v256, 0x1')"
expect_fail f23x_v256   "$(mk f23x_v256   '    new-instance v256, Ljava/lang/Object;')"
expect_fail f35c_count  "$(mk f35c_count  '    invoke-static {v0, v1, v2, v3, v4, v5}, Ljava/lang/Object;->hashCode()I')"
expect_fail f35c_reg    "$(mk f35c_reg    '    invoke-static {v16}, Ljava/lang/Object;->hashCode()I')"
expect_fail f3rc_count  "$(mk f3rc_count  '    invoke-static/range {v0 .. v300}, Ljava/lang/Object;->hashCode()I')"
expect_fail f3rc_first  "$(mk f3rc_first  '    invoke-static/range {v65536 .. v65537}, Ljava/lang/Object;->hashCode()I')"

printf '== pass=%d fail=%d ==\n' "$PASS" "$FAIL"
[ "$FAIL" -eq 0 ] || exit 1
exit 0
