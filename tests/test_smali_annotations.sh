#!/usr/bin/env bash
# Annotation handling of the smali assembler (issue #28).
#
# Every case is checked against the reference assembler: a field annotation is
# only bound to the field when the ".end field" terminator closes it, and an
# enum value has to name the type of the field ("Lclass;->name:type"), because
# the field pool is keyed by the whole reference.
set -u
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
AGENTX="${1:-$ROOT/agent-ej}"
W="$(mktemp -d /tmp/opencode/anntest.XXXXXX)"
trap 'rm -rf "$W"' EXIT
PASS=0; FAIL=0

BAKSMALI="${BAKSMALI:-$(command -v baksmali || true)}"

# mk <name> <file> <body...>   -> the directory of the fixture
mk() {
    name="$1"; file="$2"; shift 2
    d="$W/$name"; mkdir -p "$d/com/x"
    : > "$d/com/x/$file"
    for l in "$@"; do echo "$l" >> "$d/com/x/$file"; done
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
        *"$want"*) PASS=$((PASS+1)); printf '  ok   - %s rejected: %s\n' "$name" "$(echo "$out" | grep -o '"error":"[^"]*"' | head -1)";;
        *) FAIL=$((FAIL+1)); printf '  FAIL - %s wrong diagnostic (want "%s"): %s\n' "$name" "$want" "$out";;
    esac
}

expect_ok() { # expect_ok <name> <dir>
    name="$1"; d="$2"
    out="$("$AGENTX" tool smali_assemble src_dir="$d" out_dex="$W/$name.dex" 2>&1)"
    if [ $? -eq 0 ]; then
        PASS=$((PASS+1)); printf '  ok   - %s assembled\n' "$name"
    else
        FAIL=$((FAIL+1)); printf '  FAIL - %s: %s\n' "$name" "$out"
    fi
}

# baksmali <name> <dex> <expected substring>   (skipped without baksmali)
expect_baksmali() {
    name="$1"; dex="$2"; want="$3"
    if [ -z "$BAKSMALI" ]; then
        printf '  skip - %s (no baksmali)\n' "$name"; return
    fi
    out="$W/$name.out"; rm -rf "$out"
    err="$("$BAKSMALI" d "$dex" -o "$out" 2>&1)"
    if [ $? -ne 0 ]; then
        FAIL=$((FAIL+1)); printf '  FAIL - %s baksmali rejected the DEX: %s\n' "$name" "$err"; return
    fi
    if grep -rqF -- "$want" "$out"; then
        PASS=$((PASS+1)); printf '  ok   - %s: %s\n' "$name" "$want"
    else
        FAIL=$((FAIL+1)); printf '  FAIL - %s: "%s" not in the disassembly\n' "$name" "$want"; return
    fi
}

# expect_order <name> <dex> <first> <second> <before|after>
# The line of <first> has to come before (or after) the line of <second>.
expect_order() {
    name="$1"; dex="$2"; first="$3"; second="$4"; dir="$5"
    if [ -z "$BAKSMALI" ]; then
        printf '  skip - %s (no baksmali)\n' "$name"; return
    fi
    out="$W/$name.ord"; rm -rf "$out"
    if ! "$BAKSMALI" d "$dex" -o "$out" >/dev/null 2>&1; then
        FAIL=$((FAIL+1)); printf '  FAIL - %s baksmali rejected the DEX\n' "$name"; return
    fi
    f="$(awk -v pat="$first" 'index($0,pat){print FILENAME":"FNR; exit}' $(find "$out" -name '*.smali') 2>/dev/null | cut -d: -f2)"
    s="$(awk -v pat="$second" 'index($0,pat){print FILENAME":"FNR; exit}' $(find "$out" -name '*.smali') 2>/dev/null | cut -d: -f2)"
    if [ -z "$f" ] || [ -z "$s" ]; then
        FAIL=$((FAIL+1)); printf '  FAIL - %s: "%s" or "%s" is missing\n' "$name" "$first" "$second"; return
    fi
    if { [ "$dir" = before ] && [ "$f" -lt "$s" ]; } || { [ "$dir" = after ] && [ "$f" -gt "$s" ]; }; then
        PASS=$((PASS+1)); printf '  ok   - %s: "%s" is %s "%s"\n' "$name" "$first" "$dir" "$second"
    else
        FAIL=$((FAIL+1)); printf '  FAIL - %s: "%s" (line %s) is not %s "%s" (line %s)\n' \
            "$name" "$first" "$f" "$dir" "$second" "$s"
    fi
}

echo "== annotation handling (issue #28) =="

# 1. A class level annotation that follows a field stays on the class: the
#    reference assembler only binds a block to the field when ".end field"
#    closes it.
d="$(mk class_after_field A.smali \
    '.class public Lcom/x/T;' \
    '.super Ljava/lang/Object;' \
    '' \
    '.field public zzz:I' \
    '' \
    '.annotation runtime Lcom/x/Ann;' \
    '    x = 1' \
    '.end annotation' \
    '' \
    '.method public static m()V' \
    '    .registers 1' \
    '    return-void' \
    '.end method')"
expect_ok class_after_field "$d"
expect_baksmali class_after_field "$W/class_after_field.dex" '.annotation runtime Lcom/x/Ann;'
expect_order class_after_field "$W/class_after_field.dex" \
    '.annotation runtime Lcom/x/Ann;' '.field public zzz:I' before

# 2. The canonical form of baksmali: the annotation is nested in the field
#    body and closed by ".end field".
d="$(mk field_with_end_field A.smali \
    '.class public Lcom/x/T;' \
    '.super Ljava/lang/Object;' \
    '' \
    '.field public zzz:I' \
    '    .annotation runtime Lcom/x/Ann;' \
    '        x = 1' \
    '    .end annotation' \
    '.end field' \
    '' \
    '.method public static m()V' \
    '    .registers 1' \
    '    return-void' \
    '.end method')"
expect_ok field_with_end_field "$d"
expect_baksmali field_with_end_field "$W/field_with_end_field.dex" '.end field'
expect_order field_with_end_field "$W/field_with_end_field.dex" \
    '.annotation runtime Lcom/x/Ann;' '.field public zzz:I' after

# 3. An enum value without the type of the field cannot be resolved: the
#    reference assembler rejects it ("expecting COLON").
d="$(mk enum_no_type A.smali \
    '.class public Lcom/x/T;' \
    '.super Ljava/lang/Object;' \
    '' \
    '.annotation runtime Lcom/x/Ann;' \
    '    x = .enum Lcom/x/Ann;->BAD' \
    '.end annotation' \
    '' \
    '.method public static m()V' \
    '    .registers 1' \
    '    return-void' \
    '.end method')"
expect_fail enum_no_type "$d" "malformed .enum value"

# 4. The same for the initializer of a static field.
d="$(mk enum_init_no_type A.smali \
    '.class public Lcom/x/T;' \
    '.super Ljava/lang/Object;' \
    '' \
    '.field public static final E:Lcom/x/Ann; = .enum Lcom/x/Ann;->BAD' \
    '' \
    '.method public static m()V' \
    '    .registers 1' \
    '    return-void' \
    '.end method')"
expect_fail enum_init_no_type "$d" "malformed .enum initializer"

# 5. A complete enum value resolves to the field it names, not to field 0.
d="$(mk enum_value_ok A.smali \
    '.class public Lcom/x/T;' \
    '.super Ljava/lang/Object;' \
    '' \
    '.annotation runtime Lcom/x/Ann;' \
    '    x = .enum Lcom/x/Ann;->BAD:Lcom/x/Ann;' \
    '.end annotation' \
    '' \
    '.method public static m()V' \
    '    .registers 1' \
    '    return-void' \
    '.end method')"
expect_ok enum_value_ok "$d"
expect_baksmali enum_value_ok "$W/enum_value_ok.dex" 'x = .enum Lcom/x/Ann;->BAD:Lcom/x/Ann;'

# 6. The initializer of a static field resolves to the enum field and not to
#    the field that carries the initializer.
d="$(mk enum_init_ok A.smali \
    '.class public Lcom/x/T;' \
    '.super Ljava/lang/Object;' \
    '' \
    '.field public static final E:Lcom/x/Ann; = .enum Lcom/x/Ann;->BAD:Lcom/x/Ann;' \
    '' \
    '.method public static m()V' \
    '    .registers 1' \
    '    return-void' \
    '.end method')"
expect_ok enum_init_ok "$d"
expect_baksmali enum_init_ok "$W/enum_init_ok.dex" '= .enum Lcom/x/Ann;->BAD:Lcom/x/Ann;'

# 7. An enum whose class is not part of the DEX is valid: the reference
#    assembler accepts it too.
d="$(mk enum_foreign_class A.smali \
    '.class public Lcom/x/T;' \
    '.super Ljava/lang/Object;' \
    '' \
    '.annotation runtime Lcom/x/Ann;' \
    '    x = .enum Lcom/y/Missing;->BAD:Lcom/y/Missing;' \
    '.end annotation' \
    '' \
    '.method public static m()V' \
    '    .registers 1' \
    '    return-void' \
    '.end method')"
expect_ok enum_foreign_class "$d"
expect_baksmali enum_foreign_class "$W/enum_foreign_class.dex" 'x = .enum Lcom/y/Missing;->BAD:Lcom/y/Missing;'

# 8. The enum has to resolve to the field of its own class: the field pool is
#    sorted, so the index of the enum is not the index within the class.
d="$(mk enum_index A.smali \
    '.class public Lcom/x/T;' \
    '.super Ljava/lang/Object;' \
    '' \
    '.field public zzz:I' \
    '' \
    '.annotation runtime Lcom/x/Ann;' \
    '    x = .enum Lcom/x/Ann;->BAD:Lcom/x/Ann;' \
    '.end annotation' \
    '' \
    '.method public static m()V' \
    '    .registers 1' \
    '    return-void' \
    '.end method')"
expect_ok enum_index "$d"
expect_baksmali enum_index "$W/enum_index.dex" 'x = .enum Lcom/x/Ann;->BAD:Lcom/x/Ann;'

echo "----"
echo "pass: $PASS  fail: $FAIL"
[ "$FAIL" -eq 0 ]
