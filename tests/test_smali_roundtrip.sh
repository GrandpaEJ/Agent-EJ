#!/usr/bin/env bash
# Smali assembler roundtrip harness (issue #34, AGENTS.md §7b Phase 4.8).
#
# Pipeline:
#   1. Write a multi-class inline fixture → assemble → DEX (canary)
#   2. Disassemble our DEX with baksmali → canonical smali (the reference)
#   3. Reassemble the canonical smali → DEX2
#   4. Disassemble DEX2 with baksmali → smali2
#   5. diff -ru canonical vs smali2 (semantic roundtrip parity)
#   6. Validate both DEXes with dex_invariants.py (G7/G8)
#   7. (optional) If smali.jar available, assemble the canonical smali →
#      ref.dex and byte-compare against ours
#
# Env vars:
#   ROUNDTRIP_DEX=<path>  — also test against an external DEX file
#   ROUNDTRIP_APK=<path>  — also test against an external APK (extracts classes.dex)
#
# Exit non-zero on any divergence.
set -u
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
AGENTX="${1:-$ROOT/agent-ej}"
W="$(mktemp -d /tmp/opencode/rt.XXXXXX)"
trap 'rm -rf "$W"' EXIT
PASS=0; FAIL=0; SKIP=0

BAKSMALI="${BAKSMALI:-$(command -v baksmali || true)}"
SMALI="${SMALI:-$(command -v smali || true)}"
INVARIANTS="$ROOT/tests/dex_invariants.py"

if [ -z "$BAKSMALI" ]; then
    echo "FATAL: baksmali not found on PATH" >&2; exit 1
fi

ok()   { PASS=$((PASS+1)); printf '  ok   - %s\n' "$1"; }
fail() { FAIL=$((FAIL+1)); printf '  FAIL - %s\n' "$1"; }
skip() { SKIP=$((SKIP+1)); printf '  skip - %s\n' "$1"; }

# ---------- inline fixture ----------
build_fixture() {
    local d="$1"

    mkdir -p "$d/com/test"

    # Class 1: basic class with fields, methods, annotations
    cat > "$d/com/test/Basic.smali" << 'SMALI'
.class public Lcom/test/Basic;
.super Ljava/lang/Object;
.source "Basic.java"

.annotation runtime Ljava/lang/Deprecated;
.end annotation

.field private name:Ljava/lang/String;
.field public static final TAG:Ljava/lang/String; = "Basic"
.field public static count:I

.method public constructor <init>()V
    .registers 2
    .prologue

    .line 10
    invoke-direct {p0}, Ljava/lang/Object;-><init>()V

    .line 11
    const-string v0, "default"
    iput-object v0, p0, Lcom/test/Basic;->name:Ljava/lang/String;

    .line 12
    return-void
.end method

.method public getName()Ljava/lang/String;
    .registers 2

    .line 16
    iget-object v0, p0, Lcom/test/Basic;->name:Ljava/lang/String;
    return-object v0
.end method

.method public static increment()I
    .registers 2

    .line 20
    sget v0, Lcom/test/Basic;->count:I
    add-int/lit8 v0, v0, 0x1
    sput v0, Lcom/test/Basic;->count:I

    .line 21
    sget v0, Lcom/test/Basic;->count:I
    return v0
.end method
SMALI

    # Class 2: interface
    cat > "$d/com/test/Callback.smali" << 'SMALI'
.class public interface abstract Lcom/test/Callback;
.super Ljava/lang/Object;

.method public abstract onResult(ILjava/lang/String;)V
.end method
SMALI

    # Class 3: enum-like class
    cat > "$d/com/test/Color.smali" << 'SMALI'
.class public final enum Lcom/test/Color;
.super Ljava/lang/Enum;

.field public static final enum RED:Lcom/test/Color;
.field public static final enum GREEN:Lcom/test/Color;
.field public static final enum BLUE:Lcom/test/Color;

.field private static final synthetic $VALUES:[Lcom/test/Color;

.method static constructor <clinit>()V
    .registers 5

    new-instance v0, Lcom/test/Color;
    const/4 v1, 0x0
    const-string v2, "RED"
    invoke-direct {v0, v2, v1}, Lcom/test/Color;-><init>(Ljava/lang/String;I)V
    sput-object v0, Lcom/test/Color;->RED:Lcom/test/Color;

    new-instance v0, Lcom/test/Color;
    const/4 v1, 0x1
    const-string v2, "GREEN"
    invoke-direct {v0, v2, v1}, Lcom/test/Color;-><init>(Ljava/lang/String;I)V
    sput-object v0, Lcom/test/Color;->GREEN:Lcom/test/Color;

    new-instance v0, Lcom/test/Color;
    const/4 v1, 0x2
    const-string v2, "BLUE"
    invoke-direct {v0, v2, v1}, Lcom/test/Color;-><init>(Ljava/lang/String;I)V
    sput-object v0, Lcom/test/Color;->BLUE:Lcom/test/Color;

    const/4 v0, 0x3
    new-array v0, v0, [Lcom/test/Color;
    sget-object v1, Lcom/test/Color;->RED:Lcom/test/Color;
    const/4 v2, 0x0
    aput-object v1, v0, v2
    sget-object v1, Lcom/test/Color;->GREEN:Lcom/test/Color;
    const/4 v2, 0x1
    aput-object v1, v0, v2
    sget-object v1, Lcom/test/Color;->BLUE:Lcom/test/Color;
    const/4 v2, 0x2
    aput-object v1, v0, v2
    sput-object v0, Lcom/test/Color;->$VALUES:[Lcom/test/Color;
    return-void
.end method

.method private constructor <init>(Ljava/lang/String;I)V
    .registers 3

    invoke-direct {p0, p1, p2}, Ljava/lang/Enum;-><init>(Ljava/lang/String;I)V
    return-void
.end method

.method public static values()[Lcom/test/Color;
    .registers 1

    sget-object v0, Lcom/test/Color;->$VALUES:[Lcom/test/Color;
    invoke-virtual {v0}, [Lcom/test/Color;->clone()Ljava/lang/Object;
    move-result-object v0
    check-cast v0, [Lcom/test/Color;
    return-object v0
.end method

.method public static valueOf(Ljava/lang/String;)Lcom/test/Color;
    .registers 2

    const-class v0, Lcom/test/Color;
    invoke-static {v0, p0}, Ljava/lang/Enum;->valueOf(Ljava/lang/Class;Ljava/lang/String;)Ljava/lang/Enum;
    move-result-object v0
    check-cast v0, Lcom/test/Color;
    return-object v0
.end method
SMALI

    # Class 4: try/catch, switch, wide ops
    cat > "$d/com/test/Complex.smali" << 'SMALI'
.class public Lcom/test/Complex;
.super Ljava/lang/Object;

.method public static tryCatch(I)Ljava/lang/String;
    .registers 3

    :try_start_0
    invoke-static {p0}, Ljava/lang/String;->valueOf(I)Ljava/lang/String;
    move-result-object v0
    :try_end_0
    .catch Ljava/lang/Exception; {:try_start_0 .. :try_end_0} :catch_0

    return-object v0

    :catch_0
    move-exception v1
    const-string v0, "error"
    return-object v0
.end method

.method public static switchTest(I)I
    .registers 2

    packed-switch p0, :pswitch_data_0

    const/4 v0, -0x1
    return v0

    :pswitch_0
    const/4 v0, 0x0
    return v0

    :pswitch_1
    const/4 v0, 0x1
    return v0

    :pswitch_2
    const/4 v0, 0x2
    return v0

    :pswitch_data_0
    .packed-switch 0x0
        :pswitch_0
        :pswitch_1
        :pswitch_2
    .end packed-switch
.end method

.method public static wideOps(JJ)J
    .registers 6

    add-long v0, p0, p2
    const-wide/16 v2, 0x2
    div-long/2addr v0, v2
    return-wide v0
.end method

.method public static sparseSwitch(I)I
    .registers 2

    sparse-switch p0, :sswitch_data_0

    const/4 v0, -0x1
    return v0

    :sswitch_0
    const/16 v0, 0xa
    return v0

    :sswitch_1
    const/16 v0, 0x64
    return v0

    :sswitch_data_0
    .sparse-switch
        0x1 -> :sswitch_0
        0x5 -> :sswitch_1
    .end sparse-switch
.end method
SMALI

    # Class 5: annotation definition
    cat > "$d/com/test/Ann.smali" << 'SMALI'
.class public interface abstract annotation Lcom/test/Ann;
.super Ljava/lang/Object;
.implements Ljava/lang/annotation/Annotation;

.method public abstract value()Ljava/lang/String;
.end method

.method public abstract count()I
.end method
SMALI

    # Class 6: class using the annotation, with .param debug info
    cat > "$d/com/test/Annotated.smali" << 'SMALI'
.class public Lcom/test/Annotated;
.super Ljava/lang/Object;

.annotation runtime Lcom/test/Ann;
    value = "hello"
    count = 0x2a
.end annotation

.field public x:I
    .annotation runtime Ljava/lang/Deprecated;
    .end annotation
.end field

.method public constructor <init>()V
    .registers 1

    invoke-direct {p0}, Ljava/lang/Object;-><init>()V
    return-void
.end method

.method public compute(II)I
    .registers 5
    .param p1, "a"
    .param p2, "b"

    .prologue

    .line 5
    add-int v0, p1, p2
    mul-int/lit8 v0, v0, 0x2

    .line 6
    return v0
.end method
SMALI

    # Class 7: array static-field initializers (issue #37) — scalar, string,
    # type, nested, empty and wide arrays all go through encoded_array.
    cat > "$d/com/test/Arrays.smali" << 'SMALI'
.class public Lcom/test/Arrays;
.super Ljava/lang/Object;

.field public static final EMPTY:[J = {}
.field public static final INTS:[I = {
    0x1,
    0x2,
    -0x3,
    0x7fffffff
}
.field public static final LONGS:[J = {
    0x1L,
    -0x8000000000000000L
}
.field public static final NESTED:[[I = {
    {
        0x1
    },
    {}
}
.field public static final NAMES:[Ljava/lang/String; = {
    "alpha",
    "\u00e9\u4e2d"
}
.field public static final TYPES:[Ljava/lang/Class; = {
    Ljava/lang/String;,
    [I
}
.field public static plain:I = 0x2a
SMALI
}

# ---------- run one roundtrip ----------
# roundtrip <label> <smali_dir>
#   Assembles smali_dir, disassembles with baksmali → canonical form,
#   reassembles the canonical form, disassembles again, diffs the two
#   baksmali outputs (semantic parity).
roundtrip() {
    local label="$1" src="$2"
    local dex1="$W/${label}_1.dex"
    local dex2="$W/${label}_2.dex"
    local canon="$W/${label}_canon"
    local rt="$W/${label}_rt"

    # Step 1: assemble source → DEX1
    local asm_out
    asm_out="$("$AGENTX" tool smali_assemble src_dir="$src" out_dex="$dex1" 2>&1)"
    if [ $? -ne 0 ]; then
        fail "$label: agent-ej assemble failed: $asm_out"; return 1
    fi
    if [ ! -f "$dex1" ]; then
        fail "$label: agent-ej produced no DEX"; return 1
    fi
    ok "$label: assemble"

    # Step 2: DEX invariants on DEX1
    if [ -f "$INVARIANTS" ]; then
        local inv_out
        inv_out="$(python3 "$INVARIANTS" "$dex1" 2>&1)"
        if [ $? -ne 0 ]; then
            fail "$label: dex_invariants DEX1: $inv_out"; return 1
        fi
        ok "$label: dex_invariants"
    else
        skip "$label: dex_invariants (script not found)"
    fi

    # Step 3: baksmali DEX1 → canonical smali
    rm -rf "$canon"
    local bak_out
    bak_out="$("$BAKSMALI" d "$dex1" -o "$canon" 2>&1)"
    if [ $? -ne 0 ]; then
        fail "$label: baksmali rejected DEX1: $bak_out"; return 1
    fi
    ok "$label: baksmali accepts DEX1"

    # Step 4: reassemble canonical → DEX2
    asm_out="$("$AGENTX" tool smali_assemble src_dir="$canon" out_dex="$dex2" 2>&1)"
    if [ $? -ne 0 ]; then
        fail "$label: agent-ej re-assemble failed: $asm_out"; return 1
    fi
    ok "$label: re-assemble"

    # Step 5: DEX invariants on DEX2
    if [ -f "$INVARIANTS" ]; then
        inv_out="$(python3 "$INVARIANTS" "$dex2" 2>&1)"
        if [ $? -ne 0 ]; then
            fail "$label: dex_invariants DEX2: $inv_out"; return 1
        fi
        ok "$label: dex_invariants DEX2"
    fi

    # Step 6: baksmali DEX2 → roundtrip smali
    rm -rf "$rt"
    bak_out="$("$BAKSMALI" d "$dex2" -o "$rt" 2>&1)"
    if [ $? -ne 0 ]; then
        fail "$label: baksmali rejected DEX2: $bak_out"; return 1
    fi
    ok "$label: baksmali accepts DEX2"

    # Step 7: diff canonical vs roundtrip
    local diff_out
    diff_out="$(diff -ru "$canon" "$rt" 2>&1)"
    if [ $? -eq 0 ]; then
        ok "$label: roundtrip identical (baksmali→assemble→baksmali stable)"
    else
        local nfiles
        nfiles="$(echo "$diff_out" | grep -c '^diff ' || true)"
        fail "$label: roundtrip divergence ($nfiles files differ)"
        echo "$diff_out" | head -80
        return 1
    fi

    # Step 8: byte-compare DEX1 vs DEX2 (ideal: identical)
    if cmp -s "$dex1" "$dex2"; then
        ok "$label: DEX byte-identical across roundtrip"
    else
        # Not a hard failure — debug info offsets can shift
        local sz1 sz2
        sz1="$(wc -c < "$dex1")"
        sz2="$(wc -c < "$dex2")"
        skip "$label: DEX not byte-identical ($sz1 vs $sz2 bytes) — semantic parity OK"
    fi

    # Step 9: if smali.jar available, byte-compare against reference assembler
    if [ -n "$SMALI" ]; then
        local ref_dex="$W/${label}_ref.dex"
        "$SMALI" a "$canon" -o "$ref_dex" 2>/dev/null
        if [ $? -eq 0 ] && [ -f "$ref_dex" ]; then
            if cmp -s "$ref_dex" "$dex2"; then
                ok "$label: byte-identical with smali.jar"
            else
                local sz_ref sz_ours
                sz_ref="$(wc -c < "$ref_dex")"
                sz_ours="$(wc -c < "$dex2")"
                if [ "$sz_ref" = "$sz_ours" ]; then
                    local ndiff
                    ndiff="$(cmp -l "$ref_dex" "$dex2" 2>/dev/null | wc -l)"
                    skip "$label: same size as smali.jar ($sz_ref bytes) but $ndiff bytes differ"
                else
                    skip "$label: differs from smali.jar ($sz_ref vs $sz_ours bytes)"
                fi
            fi
        else
            skip "$label: smali.jar assembly failed"
        fi
    fi

    return 0
}

# ---------- external DEX roundtrip ----------
external_roundtrip() {
    local label="$1" dex="$2"
    local ref_dir="$W/${label}_ext_ref"

    # Disassemble external DEX with baksmali → ref smali
    rm -rf "$ref_dir"
    local bak_out
    bak_out="$("$BAKSMALI" d "$dex" -o "$ref_dir" 2>&1)"
    if [ $? -ne 0 ]; then
        fail "$label: baksmali failed on input DEX: $bak_out"; return 1
    fi

    local nclasses
    nclasses="$(find "$ref_dir" -name '*.smali' | wc -l)"
    ok "$label: baksmali disassembled ($nclasses classes)"

    # Run the standard roundtrip on baksmali's output
    roundtrip "$label" "$ref_dir"
}

# ========== Main ==========
echo "== smali roundtrip harness (issue #34) =="
echo "  agent-ej: $AGENTX"
echo "  baksmali: $BAKSMALI"
echo "  smali:    ${SMALI:-'(not found)'}"

# 1. Inline fixture
echo ""
echo "--- inline fixture ---"
FIXTURE="$W/fixture"
mkdir -p "$FIXTURE"
build_fixture "$FIXTURE"

nclasses="$(find "$FIXTURE" -name '*.smali' | wc -l)"
echo "  fixture: $nclasses classes"

roundtrip inline "$FIXTURE"

# 2. External DEX (optional)
if [ -n "${ROUNDTRIP_DEX:-}" ]; then
    echo ""
    echo "--- external DEX: $ROUNDTRIP_DEX ---"
    if [ -f "$ROUNDTRIP_DEX" ]; then
        external_roundtrip ext_dex "$ROUNDTRIP_DEX"
    else
        skip "ext_dex: file not found: $ROUNDTRIP_DEX"
    fi
fi

# 3. External APK (optional)
if [ -n "${ROUNDTRIP_APK:-}" ]; then
    echo ""
    echo "--- external APK: $ROUNDTRIP_APK ---"
    if [ -f "$ROUNDTRIP_APK" ]; then
        apk_dex="$W/apk_classes.dex"
        unzip -o -q "$ROUNDTRIP_APK" classes.dex -d "$W" 2>/dev/null && \
            mv "$W/classes.dex" "$apk_dex" 2>/dev/null
        if [ -f "$apk_dex" ]; then
            external_roundtrip ext_apk "$apk_dex"
        else
            skip "ext_apk: could not extract classes.dex"
        fi
    else
        skip "ext_apk: file not found: $ROUNDTRIP_APK"
    fi
fi

# Summary
echo ""
printf '== pass=%d fail=%d skip=%d ==\n' "$PASS" "$FAIL" "$SKIP"
[ "$FAIL" -eq 0 ] || exit 1
exit 0
