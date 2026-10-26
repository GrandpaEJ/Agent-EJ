# Agent-X — Handoff (after the release of v0.8.2)

**Repo:** `GrandpaEJ/Agent-X` · **Branch:** `main` (= `origin/main`)
**Released:** **v0.8.2** — PR #27 (`82ddf91`) merged, release commit `3dbd0ff`,
tag `v0.8.2` pushed.
**Status:** **All seven issues are closed and the work is on `main`.** The only
open issue is #28, a pre-existing annotation defect that was found on the way.

**Branch `fix/open-issues-13-25-24-5-22-8` was kept** (not deleted) so that the
individual commits cited in the issue comments stay reachable. It can go:
`git push origin --delete fix/open-issues-13-25-24-5-22-8`.

---

## 0. Read this first

- Repo rules (`AGENTS.md`): **featwise commits**, **Small File Rule** (max 250 LOC
  per `.c`), C11, no new deps, memory-first, semantic versioning + `git tag` per release.
- Evidence protocol: **every claim must come from a command run in-session.**
- ⚠️ **`make clean` before every build** — a stale object breaks the link.
- ⚠️ **Never commit build artifacts** (`agent-x`, `obj/`) — `.gitignore` covers them.
- ⚠️ **Never commit `CHANGELOG.md` edits from a subagent** — the lead edits it centrally.
- ⚠️ **The zig cache hides compiler diagnostics.** `zig cc` replays cached objects
  without re-emitting their warnings, so an incremental build reports "0 warnings"
  even when the code has 53. **Always `rm -rf .zig-cache` before counting
  warnings.** The true baseline is **53 pre-existing warnings** (see §6.4); none of
  them come from the files this work touched.

---

## 1. Issue inventory (evidence-based)

All seven issues that were open are closed. The commit hashes below are the ones
of the branch; on `main` they live in the squash merge `82ddf91` (PR #27), and
the branch is kept so the hashes stay reachable.

| # | Title | State |
|---|---|---|
| **5** | Native `resources.arsc` encoder | ✅ **closed** — `c1fbb13`, `ff006f8`, `9f5328a`, `0d70a1e` — see §5 |
| **13** | Native v3 APK signing: RSA signature does not verify | ✅ **closed** — `3237469`, `fb90904`, `cdf3d8f`, `ac0914f` |
| **22** | `smali_encoder`: no register range validation per instruction format | ✅ **closed** — `65e9c44` |
| **8** | Smali Assembler — Validation & Error Reporting | ✅ **closed** — `48b2507`, `e05474e` |
| **7** | Smali Assembler — Debug Info (Line Numbers & Locals) | ✅ **closed** — `55d14a4` — read §4.4 |
| **25** | `format_axml_encode`: gravity enum strings not parsed | ✅ **closed** — `8d0ffed` |
| **24** | `format_apk_decode_arsc`: incomplete resource qualifier parsing | ✅ **closed** — `cd92923` |
| **28** | *new* — annotation `.enum` written with an out-of-range field index | 🔴 **open** — see §4.5 |

---

## 2. Commits

The branch carried 18 commits; PR #27 squashed them into `82ddf91` on `main`,
and `3dbd0ff` is the release commit that opens the `0.8.2` section of the
CHANGELOG.

```
c1a75da docs(handoff): record the state of the open issues after the encoder work
0d70a1e test(arsc): add a regression test for the resource table encoder
9f5328a feat(arsc): expose the resource table encoder as a tool
ff006f8 feat(arsc): write a resource table back out of the model
c1fbb13 feat(arsc): decode a resource table into an in-memory model
e0f9d2f docs(changelog): record the resource table encoder and the issue fixes
36ac012 test(smali): add regression tests for the register limits and the diagnostics
55d14a4 fix(smali): write the debug information the way the DEX format defines it
e05474e fix(smali): reuse identical type lists and stop writing uninitialised types
48b2507 feat(smali): report syntax errors with the source line number
65e9c44 fix(smali): validate register operands against the DEX format limits
ac0914f test(apk): add a signing regression test
cdf3d8f fix(apk): report a certificate/key mismatch as an error instead of signing a broken APK
fb90904 fix(apk): refuse to sign with a certificate that does not match the key
3237469 feat(crypto): derive the RSA public key and match it against a certificate
efb7dbc fix(smali): report assembler errors through the tool layer instead of exiting
cd92923 fix(apk): parse the full ResTable_config for resource qualifiers
8d0ffed fix(axml): parse gravity enum strings when encoding AXML
8fa5495 docs(changelog): record the DEX 037 support and hardening work   <- base
```

On `main`:

```
3dbd0ff release: bump version to 0.8.2
82ddf91 feat: native resources.arsc encoder, signed APK fix and smali assembler hardening (#27)
8fa5495 docs(changelog): record the DEX 037 support and hardening work
```

---

## 3. Issue #13 — APK signing (P0). **FIXED, VERIFIED**

### 3.1 Root cause (do not re-derive)

The v3 signature machinery was **correct all along**. The bug was a **cert/key
mismatch**: `format_apk_sign_block.c` embedded the **static built-in**
`cert_der`/`pubkey_der` when no custom certificate was supplied, but signed with
the **user's** private key. apksigner derives the public key from the embedded
certificate → the signature (made with a different key) does not verify.

```
$ agent-x tool resign_apk path=t13b.apk scheme=v1,v2,v3 key=testkey_p8.pem
{"status":"success","output":"/tmp/opencode/t13b.apk.signed"}
$ apksigner verify --verbose t13b.apk.signed
DOES NOT VERIFY
ERROR: APK Signature Scheme v3 signer #1: RSA_PKCS1_V1_5_WITH_SHA256 signature over signed-data did not verify
```

The built-in certificate's public key matches a `testkey.pem` that was **deleted
from the repository** in commit `8ebc847` but survives in git history as blob
`5cafd5ecd33d73810c7d4ab78c493c30df023874`:

```
$ git show 5cafd5ecd33d73810c7d4ab78c493c30df023874 > builtin_testkey.pem
$ openssl rsa -in builtin_testkey.pem -pubout -outform DER   # 294 bytes, byte-identical to pubkey_der.h
```

### 3.2 What changed

- **`3237469` `feat(crypto)`** — new `src/crypto/der.c` (191 LOC) + `include/der.h`:
  - `der_spki_from_rsa()` builds the canonical `SubjectPublicKeyInfo` from the
    `rsa_key` limbs; **byte-identical to OpenSSL's SPKI** (2048- and 1024-bit).
  - `der_pubkey_from_cert()` **parses** the X.509 structure
    (Certificate → tbsCertificate → version/serial/sigalg/issuer/validity/subject/SPKI)
    instead of scanning for the rsaEncryption OID. The old scan only matched the
    `30 82` length form, so **1024-bit keys (0x81 form) failed**.
  - `der_cert_matches_key()` compares the two SPKIs.
  - `src/crypto/rsa.c`: `parse_pkcs8_der()` **skipped the public exponent `e`** —
    `key->e` was always zero. It is read now.
- **`fb90904` `fix(apk)`** — `apk_sign_v1()` and `apk_sign_v2_v3()` refuse to sign
  when the certificate does not match the key (`APK_SIGN_ERR_CERT_MISMATCH`,
  `include/formats.h`); `apk_build()` propagates the code instead of collapsing it
  to `-1`; `format_apk_v2.c`/`format_apk_v3.c` use `key->bits / 8` for the
  signature length (was hardcoded 256); `keys/testkey.pem` restored from the blob.
- **`cdf3d8f` `fix(apk)`** — `execute_resign_apk()` emits
  `{"status":1,"error":…,"code":"cert_key_mismatch"}` and **exits 1** (it used to
  exit 0 on failure, so scripts could not detect it); the default key is resolved
  through `resolve_default_key_path()` (`testkey.pem` → `keys/testkey.pem` →
  `<exe dir>/keys/testkey.pem` via `/proc/self/exe`).
- **`ac0914f` `test(apk)`** — `tests/test_apk_signing.sh` (see §6.3).

### 3.3 Verification

```
matching cert (2048-bit):  {"status":"success"}  → apksigner: v2: true, v3: true        ✓
mismatched (no cert):      {"error":"…cert_key_mismatch"} exit 1 → no .signed file      ✓
zero-config (clean dir):   keys/testkey.pem auto-found → v2: true, v3: true             ✓
1024-bit key + cert:       cert parse OK → v2: true, v3: true                           ✓
v1 path, mismatched cert:  rejected, exit 1                                            ✓
```

### 3.4 Still open (out of scope of #13)

The **v1 (JAR) signing path uses SHA-1**, which Android 7+ (targetSdk ≥ 24)
rejects, and `jarsigner` reports "nonexistent signed entries" for every entry —
the v1 zip writer likely produces entries jarsigner cannot see. Open a separate
issue for the v1 path.

---

## 4. Issues #22 / #8 / #7 — smali assembler

### 4.1 #22 — register range validation (`65e9c44`)

`src/android/smali/smali_validate.c` (124 LOC) holds a 27-row table with the
limits of every DEX bytecode format; `smali_encoder.c` calls
`smali_validate_insn()` for every instruction and aborts the encoding with a
diagnostic instead of truncating the operand. The F_35c argument registers are
resolved once into an `invoke_regs[5]` array shared by the validator and the encoder.

The old ad-hoc checks were wrong in both directions: F_22x's 16-bit `vB` was
never checked, F_3rc and F_31i were not checked at all, and F_51l was checked
against a limit that does not apply to it.

| fmt | format | vA | vB | vC |
|---|---|---|---|---|
| 0 | F_10x | – | – | – |
| 1, 21 | F_12x | 15 | 15 | – |
| 2 | F_11n | 15 | – | – |
| 3 | F_11x | 255 | – | – |
| 4, 5, 6 | F_10t/20t/30t | – | – | – |
| 7, 19 | F_21c | 255 | – | – |
| 8 | F_22c | 15 | 15 | – |
| 9 | F_21t | 255 | – | – |
| 10 | F_22t | 15 | 15 | – |
| 11 | F_21s | 255 | – | – |
| 12 | F_22b | 255 | 255 | – |
| 13 | F_23x | 255 | 255 | 255 |
| 14 | F_35c | count 0..5 | arg regs ≤15 | – |
| 15 | F_3rc | 255 (count) | – | 65535 (first reg) |
| 16 | F_31i | 255 | – | – |
| 18 | F_31t | 255 | – | – |
| 22 | F_22s | 15 | 15 | – |
| 23 | F_22x | 255 | 65535 | – |
| 24 | F_32x | 65535 | 65535 | – |
| 25 | F_31c | 255 | – | – |
| 26 | F_51l | 255 | – | – |

Rows 17 and 20 are unused format numbers.

**Verification:** `tests/test_smali_register_limits.sh` → **21/21** (9 valid
instructions accepted, 12 invalid rejected with a precise message).

### 4.2 #8 — syntax errors with the source line (`48b2507`)

- `smali_line_t` (`smali_lexer.h/.c`) tracks the physical source line of the
  construct being parsed; the counting is amortised over the file and a rewind
  inside a method body is cheap (the body is counted from its own start, and the
  file counter is moved to the end of the method with `smali_line_jump()`).
- `smali_fail_at(line, …)` produces `file:line: message`; the file name comes
  from `smali_set_source_name()` in `parse_smali_file_path()`.
- Unknown instructions and unknown directives are **errors** now (`.restart local`,
  which baksmali emits, is accepted); a malformed `.class`, `.field`, `.method` or
  `.annotation` is an error; a parse error **aborts the assembly**
  (`smali_assemble_n()`), instead of emitting a DEX that silently lost the file.
- `smali_insn_t.src_line` records the source line of every instruction.

**Side fix in the same commit — the method signature was silently truncated.**
`.method public static a(I, I)V` was read as the first token only (`a(I,`), so the
DEX carried `a(I)V` and **69 of the 70 parameters were dropped**. The signature is
now read as a whole and normalised to the DEX form (`normalize_method_spec()`).
The reference assembler rejects that spelling, so this is a deliberate leniency.

**Verification:** `tests/test_smali_diagnostics.sh` → **7/7** (each case reports the
expected file and line).

### 4.3 #8 — type lists (`e05474e`)

New `src/android/smali/smali_typelist.c` (+ `.h`) keeps a registry of the
type_list items: a list that was already written is **reused** instead of being
emitted again, for the proto parameter lists **and** the class interface lists.
Before, a file with the protos `(II)V`, `(II)I`, `(II)J` plus a class that
implements an interface carried **three** type lists for two distinct contents;
now it carries two, and the map_list entry agrees with the items in the file.

The same code read **uninitialised stack memory**: the parameter type indices were
collected in a fixed array of 256 entries while the count was unbounded, so a
proto with more than 256 parameters made the writer compare and emit the entries
past the end of the array. Proof: poisoning the array beyond its end in the old
code puts the invalid type index **43690** into the file. The indices now live in
a buffer that grows with the parameter count.

**Verification:** a 300-parameter proto is assembled correctly and is parsed by
baksmali; the type-list count is checked with `dextl.py` (see §6.2).

### 4.4 #7 — debug info (`55d14a4`) — **the previous handoff was wrong about this one**

The previous handoff said #7 was "already implemented → just close the issue".
**It was not.** The debug information was produced, but almost none of it was
usable:

| defect | effect |
|---|---|
| the map_list had **no `debug_info_item` (0x2003) entry** | the map did not describe the items in the file |
| the parameter and local **names were never added to the string pool** | the debug info referred to the index `0xffffffff` |
| the string/type indices were written as **plain uleb128** | the format defines them as **uleb128p1** (encoded = index + 1, which is how NO_INDEX becomes 0) → **every name was off by one** |
| an instruction without a `.line` produced **no address advance** | the positions table was incomplete |
| `.epilogue` was parsed and then **dropped** | `DBG_SET_EPILOGUE_BEGIN` (0x08) was never emitted |
| `.local v0, "name": <type>` was parsed as type `":"` and signature `<type>` | the local's type was garbage |

Spec reference (AOSP "Dalvik executable format", fetched during this session):
`line_start` and `parameters_size` are **uleb128**; `parameter_names`,
`name_idx`, `type_idx`, `sig_idx` are **uleb128p1**.

**Verification:** two fixtures (`.line` + `.local` + `.param` + `.prologue` +
`.epilogue`) were assembled with `smali.jar` and with agent-x; **baksmali
disassembles both files to byte-identical smali**, with the same parameter names,
local names and local types.

### 4.5 Pre-existing defect found, **not** fixed — issue #28 is open

`ann3` and `ann4` produce a DEX that **baksmali rejects**:

```
java.lang.IndexOutOfBoundsException: Invalid field index 0, not in [0, 0)
    at DexBackedFieldReference.getDefiningClass(DexBackedFieldReference.java:53)
    at BaksmaliWriter.writeEnum(BaksmaliWriter.java:309)
```

An annotation `.enum` value is written with a field index that is out of range.
Reproduced identically on `8fa5495` (verified with `git stash`), so it is **not**
caused by this branch. It is in the annotation writer, not in the parser.

It was reported as **issue #28** with the reproduction steps. The writer has to
resolve the enum to a real field of the annotation's class and fall back to a
defined behaviour when the class does not declare it.

### 4.6 Known pre-existing problems in this area (found, not fixed)

- **Small File Rule violations**: `smali_writer.c` 1229 LOC, `smali_pool.c` 755,
  `smali_parser.c` 633, `smali_encoder.c` 292 (limit is 250). The new files stay
  inside the rule: `smali_typelist.c` 90, `smali_validate.c` 124, `der.c` 192.
- `smali_pool.c` `comp_field_key` / `comp_method_key` strict-weak-ordering concern
  (malformed keys use `strcmp` while valid keys use a numeric index) — a known LOW
  finding from the earlier PR review, still open.

---

## 5. Issue #5 — Native `resources.arsc` encoder. **DONE, VERIFIED**

### 5.1 What was there before

`src/android/arsc/format_arsc_build.c` had `arsc_build()`, but it only **patched
the global string pool** and copied the rest of the original file byte for byte.
It was not a resource table writer, and the old parser kept only the **last**
configuration of every type, so the other configurations were unreachable.

### 5.2 The new code

| File | LOC | What it does |
|---|---|---|
| `src/android/arsc/format_arsc_model.h/.c` | 122 / 211 | the model: global value pool, packages, types, variants, entries, map items + `arsc_model_set_string()` / `arsc_model_set_int()` |
| `src/android/arsc/format_arsc_load.c` | 186 | ARSC → model (string pool, package, type spec, type chunk, entries) |
| `src/android/arsc/format_arsc_pool.c/.h` | 48 | string pool reader and the UTF-16 package name |
| `src/android/arsc/format_arsc_fmt.h` | 116 | little endian accessors, UTF-8 ↔ UTF-16 |
| `src/android/arsc/format_arsc_buf.c/.h` | 83 | the growable byte buffer of the writer |
| `src/android/arsc/format_arsc_write.c` | 94 | `arsc_encode()`: table, package chunks |
| `src/android/arsc/format_arsc_write_pool.c` | 78 | the string pools |
| `src/android/arsc/format_arsc_write_entry.c` | 96 | type spec, type chunk, entries |
| `src/tools/tool_arsc.c` | 191 | the `arsc_encode` and `arsc_info` tools |

The old `format_arsc_parse.c` / `format_arsc_build.c` are untouched: the AXML
decoder still uses them, the encoder is a second, independent path.

### 5.3 The format details that cost the most time

- **`ResTable_map` is 12 bytes, not 8**: a map item is a `name` (u32) plus a full
  `Res_value` (size, res0, dataType, data). Reading it as 8 bytes shifts every
  complex entry and loses 6,576 bytes in the `attr` type of the test APK.
- **The type spec carries `res1`**: aapt2 stores the config flags of the type in
  the `uint16` at offset 10 of the `ResTable_typeSpec` chunk (0x0001 for `anim`,
  0x0057 for `string`, …). It is not padding — dropping it changes 16 bytes of
  the test APK.
- **The entry offsets are relative to `entriesStart`**, not to the chunk start.
- **The package name is UTF-16** and has to be decoded to UTF-8 and back; a
  one-byte-per-code-unit copy leaves the tail of the buffer uninitialised and
  writes garbage after the terminator (this broke 6 of the 17 test APKs).
- **The package id is a `uint32`**, not a byte — writing one byte shifts the
  whole package header.

### 5.4 Verification

```
17 real APKs round tripped      → 17/17 byte identical (cmp, not a dump)
GA Notification Monitor.apk    → 1,024,736 bytes in, 1,024,736 bytes out, cmp clean
independent oracle dump        → 21,814 lines, 0 differences
fixture from tests/arsc_build  → byte identical
patched APK                    → aapt2: string/app_name = "Agent-X Patched"
                                 apktool: <string name="app_name">Agent-X Patched</string>
```

The test script is `tests/test_arsc_encode.sh` (**22/22**). It takes the path of
the binary as its first argument and reads the APKs to round trip from
`ARSC_TEST_APKS` (newline separated, so paths with spaces work):

```bash
ARSC_TEST_APKS="$(ls ~/Downloads/Telegram\ Desktop/*.apk)" ./tests/test_arsc_encode.sh
```

`tests/arsc_dump.py` is the independent decoder used as the oracle and
`tests/arsc_build.py` builds a fixture without aapt2.

### 5.5 What is still missing (the next step)

The encoder writes a table; it does not **compile** `res/values/*.xml` into one.
`decode_apk` writes the resources as XML and `build_apk` copies the original
`resources.arsc` back, so a value that was edited in `res/values/strings.xml`
is still lost. An XML → ARSC compiler (the aapt2 half of the job) is the natural
follow-up; the model and the writer are the part it needs.

---

## 6. Test assets (all in the repository now)

### 6.1 Committed test scripts

| Script | What it does | Result |
|---|---|---|
| `tests/test_arsc_encode.sh` | the resource table encoder: the fixture round trip, the oracle dump, a string and an integer patch, `arsc_info`, plus the real APKs of `ARSC_TEST_APKS` | **22/22** |
| `tests/test_apk_signing.sh` | 12 cases: matching cert signs and verifies with apksigner (v2 + v3), a foreign cert is refused, no cert is refused, the v1 path refuses a foreign cert, no half-written output is left. Skips the apksigner checks when it is not installed. | **12/12** |
| `tests/test_smali_register_limits.sh` | 21 instructions whose registers are inside/outside the limit of their DEX format. | **21/21** |
| `tests/test_smali_diagnostics.sh` | 7 broken files → each must produce a `file:line: message` diagnostic. | **7/7** |
| `tests/dex_invariants.py` | G7 (a section with size 0 has offset 0), G8 (4-byte alignment), header `file_size` vs the real size. | OK on all fixtures |
| `tests/arsc_dump.py` | an independent decoder, used as the oracle of the encoder test | — |
| `tests/arsc_build.py` | builds a resource table without aapt2, the fixture of the encoder test | — |

Each script takes the path of the binary as its first argument and defaults to
`./agent-x` in the repository root.

### 6.2 Helper scripts (in `/tmp/opencode`, ephemeral — copy them into `tests/` if wanted)

| Script | What it does |
|---|---|
| `dexinv.py` | same as `tests/dex_invariants.py` (already copied) |
| `dextl.py <dex> [expected]` | counts the type_list items of a DEX and compares them with the map entry |
| `dbgdecode.py <dex>` | decodes a debug_info section (my own decoder — it reads the indices as plain uleb128, so it disagrees with the format on the uleb128p1 fields; use baksmali instead) |
| `dbgcmp.py <a.dex> <b.dex>` | compares the debug_info sections of two DEX files |

### 6.3 Current regression status

Run on the merged `main` of the **v0.8.2** release, with a cold build
(`rm -rf .zig-cache && make clean && make release`):

```
27 smali fixtures assemble           → 27 OK, 0 FAIL
28 DEX files pass the G7/G8 invariants → 28 OK
baksmali parses 26 of them            → ann3/ann4 fail (pre-existing, §4.5 → #28)
tests/test_apk_signing.sh             → 12 pass, 0 fail
tests/test_smali_register_limits.sh   → 21 pass, 0 fail
tests/test_smali_diagnostics.sh       → 7 pass, 0 fail
tests/test_arsc_encode.sh             → 22 pass, 0 fail
```

Fixture trees live in `/tmp/opencode/<name>/`: `t1 t2 t3 t4 ann ann2 ann3 ann4
bulk cls big huge p2 p3 p4 mref onlymethods fieldkey x_field_arrow_nocolon
x_enum_qmark x_method_arrow_noparen proto70` plus the new ones `tldedup`
(type-list dedup), `proto300`/`proto300b` (>256 parameters), `dbgcmp`/`dbg2`
(debug info, compared against smali.jar).

### 6.4 Build and warnings

```
cd /home/grandpa/me/code/c-lang/Agent-X
rm -rf .zig-cache && make clean && make release   # ~328 KB static
```

**53 warnings, all pre-existing** (none from the files this work touched):

| file | count |
|---|---|
| `src/android/axml/format_axml_output.c` | 30 (`-Wmissing-field-initializers`) |
| `src/android/dex/format_dex_smali_method.c` | 12 |
| `src/android/axml/format_axml_encode.c` | 3 |
| `src/android/arsc/format_arsc_toml.c` | 3 |
| `src/formats/zip/format_zip.c` | 1 |
| `src/android/smali/smali_writer.c` | 1 (`annot_set_ref_list_count` unused) |
| `src/android/smali/smali_lexer.c` | 1 (`has_colon` unused) |
| `src/android/axml/format_axml_strings.c` | 1 |
| `src/android/arsc/format_arsc_build.c` | 1 |

They were left alone (out of scope) but they are real.

The sanitizer build is clean as well:

```bash
rm -rf .zig-cache && make clean && make CC="gcc -fsanitize=address,undefined -fno-omit-frame-pointer -g" \
                                            LDFLAGS="-fsanitize=address,undefined"
ARSC_TEST_APKS="…" ./tests/test_arsc_encode.sh     # 22/22, no ASan/UBSan/LSan report
```

---

## 7. What is left

Everything that was on the list is done:

| # | Item | State |
|---|---|---|
| 1 | Issue #5 — the native `resources.arsc` encoder | ✅ done, §5 |
| 2 | Open an issue for the ann3/ann4 annotation defect | ✅ **#28** is open |
| 3 | Close #13, #22, #8, #7, #25, #24, #5 on GitHub | ✅ all closed with evidence |
| 4 | CHANGELOG entries | ✅ `e0f9d2f`, under `## [0.8.2]` |
| 5 | Version bump + tag | ✅ `3dbd0ff` + `v0.8.2` pushed |
| 6 | Full regression before pushing | ✅ see §6.3 — 27/28/12/21/7/22 all green, ASan clean |
| 7 | Push + PR + merge | ✅ PR #27 merged as `82ddf91` |

### The next pieces of work

1. **Issue #28** — the annotation `.enum` writer (§4.5). Small, isolated, and it
   breaks baksmali on the `ann3`/`ann4` fixtures.
2. **The v1 (JAR) signing path** (§3.4) — it still uses SHA-1, which Android 7+
   rejects, and `jarsigner` reports "nonexistent signed entries" for every
   entry. Needs its own issue.
3. **An XML → ARSC compiler** (§5.5) — the natural follow-up to the encoder:
   `decode_apk` writes the resources as XML and `build_apk` copies the original
   `resources.arsc` back, so a value that was edited in `res/values/strings.xml`
   is still lost. The model and the writer are the part a compiler needs.
4. **The Small File Rule violations** (§4.6) — `smali_writer.c` 1229 LOC,
   `smali_pool.c` 755, `smali_parser.c` 633, `smali_encoder.c` 292 against a
   limit of 250.
5. **`smali_pool.c` `comp_field_key` / `comp_method_key`** — the strict weak
   ordering concern (malformed keys use `strcmp` while valid keys use a numeric
   index), a known LOW finding from the earlier PR review.

### The full regression, for the next run

```bash
cd /home/grandpa/me/code/c-lang/Agent-X
rm -rf .zig-cache && make clean && make release
for t in t1 t2 t3 t4 ann ann2 ann3 ann4 bulk cls big huge p2 p3 p4 mref onlymethods \
         fieldkey x_field_arrow_nocolon x_enum_qmark x_method_arrow_noparen proto70 \
         tldedup proto300 proto300b dbgcmp dbg2; do
  d=/tmp/opencode/$t; [ -d "$d" ] || continue
  ./agent-x tool smali_assemble src_dir=$d out_dex=/tmp/opencode/final_$t.dex >/dev/null 2>&1 \
    && echo "OK $t" || echo "FAIL $t"
done
./tests/test_apk_signing.sh
./tests/test_smali_register_limits.sh
./tests/test_smali_diagnostics.sh
ARSC_TEST_APKS="$(ls ~/Downloads/Telegram\ Desktop/*.apk)" ./tests/test_arsc_encode.sh
python3 tests/dex_invariants.py /tmp/opencode/final_*.dex
```

Plus an **ASan/UBSan/LSan** pass (cold cache, so the diagnostics are not masked):

```bash
rm -rf .zig-cache && make clean && make CC="gcc -fsanitize=address,undefined -fno-omit-frame-pointer -g" \
                                            LDFLAGS="-fsanitize=address,undefined"
```

Plus `baksmali d` on the output and the DEX G7/G8 invariants.

---

## 8. Environment cheat-sheet

```bash
# build (cold cache, so the warnings are real)
cd /home/grandpa/me/code/c-lang/Agent-X && rm -rf .zig-cache && make clean && make release

# tools
/home/grandpa/.local/bin/baksmali          # reference disassembler
/home/grandpa/.local/bin/smali              # reference assembler
/home/grandpa/Android/Sdk/build-tools/35.0.0/apksigner    # authoritative APK verifier
/home/grandpa/Android/Sdk/build-tools/35.0.0/aapt2        # ARSC/APK oracle
/home/grandpa/.local/bin/apktool            # APK decode oracle

# signing test material
cd /tmp/opencode
openssl genrsa -traditional -out testkey.pem 2048
openssl pkcs8 -topk8 -nocrypt -in testkey.pem -out testkey_p8.pem
openssl req -new -x509 -key testkey.pem -subj "/CN=Test" -days 3650 -outform DER -out testcert.der
# NB: the tool's rsa_load_key only parses PKCS#8 ("BEGIN PRIVATE KEY"), not PKCS#1.

# sample APKs
~/Downloads/Telegram\ Desktop/GA\ Notification\ Monitor.apk     # rich resource qualifiers
~/Downloads/Telegram\ Desktop/base\ (9).apk                      # 4.8 MB resource table
~/me/code/rust/ZeroBot/waifuhost-apk/app/build/outputs/apk/release/app-release-unsigned.apk
```

**DEX constraints used throughout:** G7 (a section with size 0 must have offset 0),
G8 (sections are 4-byte aligned), G15 (`utf16_size` must match the decoded length).
