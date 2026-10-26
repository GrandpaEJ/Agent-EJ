# Agent-X — Handoff (after the release of v0.8.3)

**Repo:** `GrandpaEJ/Agent-X` · **Branch:** `main` (= `origin/main`) at `6834c06`,
tag **`v0.8.3`** pushed.
**Released:** **v0.8.3** — PR #29 (`b98d6cd`) merged, release commit `6834c06`,
tag `v0.8.3` pushed.
**Status:** **Every issue is closed** (#5 #7 #8 #13 #22 #24 #25 #26 #28). One new
issue is open: **#30** — the v1 (JAR) signing path (SHA-1 + nonexistent signed
entries), found during #13 and verified again before opening.

**Branch `fix/issue-28-annotation-enum` was deleted** on merge (local + remote).
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
  warnings.** The true baseline is **53 pre-existing warnings** (§6.4); none of
  them come from the files this work touched. Re-confirmed on v0.8.3: **53**.
- ⚠️ **The release-commit pattern**: a release commit only inserts
  `---` + `## [x.y.z] - <date>` after `## [Unreleased]` in `CHANGELOG.md`
  (see `git show 3dbd0ff` / `6834c06`). The version string lives **only** in
  the CHANGELOG, nowhere in the source.

---

## 1. Issue inventory (evidence-based)

| # | Title | State |
|---|---|---|
| **5** | Native `resources.arsc` encoder | ✅ closed — `c1fbb13`…`0d70a1e`, PR #27 — §5 |
| **13** | Native v3 APK signing: RSA signature does not verify | ✅ closed — `3237469`…`ac0914f`, PR #27 — §3 |
| **22** | `smali_encoder`: no register range validation per instruction format | ✅ closed — `65e9c44`, PR #27 — §4.1 |
| **8** | Smali Assembler — Validation & Error Reporting | ✅ closed — `48b2507`, `e05474e`, PR #27 — §4.2 |
| **7** | Smali Assembler — Debug Info (Line Numbers & Locals) | ✅ closed — `55d14a4`, PR #27 — §4.4 |
| **25** | `format_axml_encode`: gravity enum strings not parsed | ✅ closed — `8d0ffed`, PR #27 |
| **24** | `format_apk_decode_arsc`: incomplete resource qualifier parsing | ✅ closed — `cd92923`, PR #27 |
| **26** | DEX 037 support, correct smali output paths, crash/bounds hardening | ✅ closed — PR #26 |
| **28** | annotation `.enum` written with an out-of-range field index | ✅ **closed** — PR #29 (`b98d6cd`), released as v0.8.3 — §4.5 |
| **30** | v1 (JAR) signing: SHA-1 + every entry "nonexistent signed entry" | 🔴 **open** — §8 |

---

## 2. Commits

PR #29 squash-merged the 8 commits of `fix/issue-28-annotation-enum` into
`main` as `b98d6cd`; `6834c06` is the release commit that opens the `0.8.3`
section of the CHANGELOG.

On `main`:

```
6834c06 release: bump version to 0.8.3
b98d6cd fix(smali): annotation .enum field index, annotation binding and a use-after-free (#28) (#29)
d89f1dd docs(handoff): record the v0.8.2 release and what is left
3dbd0ff release: bump version to 0.8.2
82ddf91 feat: native resources.arsc encoder, signed APK fix and smali assembler hardening (#27)
```

### 2b. Issue #28 — the annotation fixes (PR #29)

Seven fixes/tests + one docs commit, one logical change each; on `main` they
live in the squash merge `b98d6cd`:

```
f4fbc84 docs(handoff): record the #28 fixes and the state of the branch
d98f4c3 docs(changelog): record the annotation fixes
7ec3113 test(smali): add regression tests for the annotation handling
e614f4a fix(smali): resolve the enum field reference through the field pool
78aa914 fix(smali): reject a malformed .enum value
0a5d3d9 fix(smali): bind a field annotation only when ".end field" closes it
c6f709c refactor(smali): move the annotation parsing into its own module
569d3f8 fix(smali): free the dispatch token only once in the class parser
```

---

## 3. Issue #13 — APK signing (P0). **FIXED, VERIFIED**

### 3.1 Root cause (do not re-derive)

The v3 signature machinery was **correct all along**. The bug was a **cert/key
mismatch**: `format_apk_sign_block.c` embedded the **static built-in**
`cert_der`/`pubkey_der` when no custom certificate was supplied, but signed with
the **user's** private key. apksigner derives the public key from the embedded
certificate → the signature (made with a different key) does not verify.

### 3.2 What changed (PR #27)

- **`3237469` `feat(crypto)`** — new `src/crypto/der.c` (191 LOC) + `include/der.h`:
  - `der_spki_from_rsa()` builds the canonical `SubjectPublicKeyInfo` from the
    `rsa_key` limbs; **byte-identical to OpenSSL's SPKI** (2048- and 1024-bit).
  - `der_pubkey_from_cert()` **parses** the X.509 structure instead of scanning
    for the rsaEncryption OID (the old scan only matched `30 82`, so 1024-bit
    keys failed).
  - `der_cert_matches_key()` compares the two SPKIs.
  - `src/crypto/rsa.c`: `parse_pkcs8_der()` skipped the public exponent `e`; it
    is read now.
- **`fb90904` `fix(apk)`** — both signing paths refuse a cert/key mismatch
  (`APK_SIGN_ERR_CERT_MISMATCH`); signature length follows `key->bits / 8`.
- **`cdf3d8f` `fix(apk)`** — `execute_resign_apk()` reports `cert_key_mismatch`
  and **exits 1** (used to exit 0 on failure); default key resolution.
- **`ac0914f` `test(apk)`** — `tests/test_apk_signing.sh`.

### 3.3 Verification

```
matching cert (2048-bit):  {"status":"success"}  → apksigner: v2: true, v3: true        ✓
mismatched (no cert):      {"error":"…cert_key_mismatch"} exit 1 → no .signed file      ✓
zero-config (clean dir):   keys/testkey.pem auto-found → v2: true, v3: true             ✓
1024-bit key + cert:       cert parse OK → v2: true, v3: true                           ✓
v1 path, mismatched cert:  rejected, exit 1                                            ✓
```

### 3.4 The v1 (JAR) path — **now issue #30** (§8)

Still broken: SHA-1 digest (Android 7+ rejects) and jarsigner reports
"nonexistent signed entries" for every entry. Reproduced on v0.8.3 with
evidence and filed as **issue #30** with acceptance criteria.

---

## 4. Issues #22 / #8 / #7 / #28 — smali assembler

### 4.1 #22 — register range validation (`65e9c44`)

`src/android/smali/smali_validate.c` (124 LOC) holds a 27-row table with the
limits of every DEX bytecode format; `smali_encoder.c` calls
`smali_validate_insn()` for every instruction and aborts the encoding with a
diagnostic instead of truncating the operand. Verification:
`tests/test_smali_register_limits.sh` → **21/21**.

### 4.2 #8 — syntax errors with the source line (`48b2507`)

- `smali_line_t` tracks the physical source line; `smali_fail_at(line, …)`
  produces `file:line: message`.
- Unknown instructions/directives are errors (`.restart local` accepted);
  a parse error **aborts the assembly** instead of silently dropping the file.
- Side fix: the method signature was silently truncated (`.method a(I, I)V`
  carried 1 of 70 parameters); now read as a whole and normalised.
- Verification: `tests/test_smali_diagnostics.sh` → **7/7**.

### 4.3 #8 — type lists (`e05474e`)

`smali_typelist.c` reuses identical type_list items (proto parameter lists and
class interface lists). Also fixed an **uninitialised stack read**: parameter
type indices lived in a fixed 256-entry array with an unbounded count.

### 4.4 #7 — debug info (`55d14a4`)

Six defects, all fixed: no `debug_info_item` map entry, names never added to
the string pool, plain uleb128 instead of **uleb128p1**, no address advance
without `.line`, `.epilogue` dropped, `.local` type parsed as `":"`.
Verified: two fixtures assemble identically under `smali.jar` and agent-x;
baksmali disassembles both to byte-identical smali.

### 4.5 #28 — the annotation `.enum` writer. **FIXED, VERIFIED, RELEASED (v0.8.3)**

Four defects, all fixed in PR #29 (`b98d6cd`):

1. **The enum field index was 0** — a value without the type was never added to
   the field pool (`0xFFFFFFFF` lookup, index 0 written); baksmali threw
   `IndexOutOfBoundsException: Invalid field index 0`. The malformed value is
   rejected now with `file:line: message`, like the reference assembler.
2. **The static field initializer had the same defect** — the reference was only
   in the string pool, so the initializer pointed at the field that carries it;
   it enters the field pool now (`pool_add_enum_ref()`).
3. **A use after free in the class parser** — the `.field` branch and the class
   parser both released the dispatch token (ASan-confirmed); freed once now.
4. **The binding rule** — a `.annotation` after `.field` belongs to the field
   only when `.end field` closes it; the block is parsed on its own and handed
   to the field only when the terminator follows.

The annotation parsing moved into `src/android/smali/smali_annot.c` (199 LOC,
inside the Small File Rule): the class/field/method parsers shared three copies
of the same loop.

**Verification:** `tests/test_smali_annotations.sh` → **16/16** (checks the
*position* of the annotation in the disassembly). Against the old code **5 of 14
assertions fail**, so the test does detect the defects. The `ann3`/`ann4`
fixtures were corrected to the valid enum syntax (they were invalid smali — the
reference assembler rejected them).

### 4.6 Known pre-existing problems in this area (found, not fixed)

- **Small File Rule violations**: `smali_writer.c` 1233 LOC, `smali_pool.c` 767,
  `smali_insn_parser.c` 526, `smali_parser.c` 512 (limit is 250).
- `smali_pool.c` `comp_field_key` / `comp_method_key` strict-weak-ordering
  concern (malformed keys use `strcmp` while valid keys use a numeric index).

---

## 5. Issue #5 — Native `resources.arsc` encoder. **DONE, VERIFIED**

The encoder decodes a resource table into an in-memory model and writes it back:
`arsc_encode()` emits the string pools, the `ResTable_package` chunk and, per
type, the `ResTable_typeSpec` + one `ResTable_type` per configuration.

### 5.1 The format details that cost the most time

- **`ResTable_map` is 12 bytes, not 8** (a `name` u32 plus a full `Res_value`).
- **The type spec carries `res1`** (config flags at offset 10, not padding).
- **Entry offsets are relative to `entriesStart`**, not the chunk start.
- **The package name is UTF-16** — a one-byte-per-code-unit copy writes garbage
  after the terminator (broke 6 of 17 test APKs).
- **The package id is a `uint32`**, not a byte.

### 5.2 Verification

```
17 real APKs round tripped      → 17/17 byte identical (cmp)
GA Notification Monitor.apk    → 1,024,736 bytes in/out, cmp clean
independent oracle dump        → 21,814 lines, 0 differences
patched APK                    → aapt2 + apktool see the patched string
```

`tests/test_arsc_encode.sh` → **22/22**; takes the binary as first argument,
the APKs from `ARSC_TEST_APKS` (newline separated).
`tests/arsc_dump.py` = independent oracle; `tests/arsc_build.py` = fixture.

### 5.3 What is still missing (the next big feature)

The encoder writes a table; it does **not compile** `res/values/*.xml` into
one. `decode_apk` writes resources as XML and `build_apk` copies the original
`resources.arsc` back, so a value edited in `res/values/strings.xml` is still
lost. An XML → ARSC compiler (the aapt2 half) is the natural follow-up; the
model and the writer are the part it needs.

---

## 6. Test assets (all in the repository now)

### 6.1 Committed test scripts

| Script | What it does | Result on v0.8.3 |
|---|---|---|
| `tests/test_arsc_encode.sh` | resource table encoder: fixture round trip, oracle dump, string/int patch, `arsc_info`, real APKs of `ARSC_TEST_APKS` | **22/22** |
| `tests/test_apk_signing.sh` | 12 cases incl. apksigner v2+v3 verification | **12/12** |
| `tests/test_smali_register_limits.sh` | 21 instructions inside/outside the limit of their DEX format | **21/21** |
| `tests/test_smali_diagnostics.sh` | 7 broken files → `file:line: message` each | **7/7** |
| `tests/test_smali_annotations.sh` | 8 fixtures, position of the annotation checked in the disassembly | **16/16** |
| `tests/dex_invariants.py` | G7, G8, header `file_size` vs real size | OK on all fixtures |
| `tests/arsc_dump.py` / `tests/arsc_build.py` | independent oracle / fixture builder | — |

Each script takes the binary path as its first argument, defaults to `./agent-x`.

### 6.2 Helper scripts (in `/tmp/opencode`, ephemeral — copy into `tests/` if wanted)

| Script | What it does |
|---|---|
| `dextl.py <dex> [expected]` | counts the type_list items and compares with the map entry |
| `dbgdecode.py <dex>` | decodes a debug_info section (reads uleb128p1 fields as plain uleb128 — use baksmali instead) |
| `dbgcmp.py <a.dex> <b.dex>` | compares the debug_info sections of two DEX files |

### 6.3 Current regression status — **v0.8.3** (`6834c06`, cold build)

```
39 smali fixtures assemble            → 39 OK, 0 FAIL
baksmali parses all of them           → 39 OK, 0 FAIL
DEX G7/G8 invariants (rel_*.dex)      → 0 failures
tests/test_apk_signing.sh             → 12 pass, 0 fail
tests/test_smali_register_limits.sh   → 21 pass, 0 fail
tests/test_smali_diagnostics.sh       → 7 pass, 0 fail
tests/test_smali_annotations.sh       → 16 pass, 0 fail
tests/test_arsc_encode.sh             → 22 pass, 0 fail
cold-build warnings                   → 53 (unchanged pre-existing baseline)
```

Fixture trees live in `/tmp/opencode/<name>/`: `t1 t2 t3 t4 ann ann2 ann3 ann4
bulk cls big huge p2 p3 p4 mref onlymethods fieldkey x_field_arrow_nocolon
x_enum_qmark x_method_arrow_noparen proto70 tldedup proto300 proto300b dbgcmp
dbg2 b1 b2 b3 b4 b5 b6 b7 b8 ann5 ann6 ann7 r2`.

### 6.4 Build and warnings

```
cd /home/grandpa/me/code/c-lang/Agent-X
rm -rf .zig-cache && make clean && make release   # ~328 KB static
```

**53 warnings, all pre-existing**:

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

Sanitizer build:

```bash
rm -rf .zig-cache && make clean && make CC="gcc -fsanitize=address,undefined -fno-omit-frame-pointer -g" \
                                            LDFLAGS="-fsanitize=address,undefined"
ARSC_TEST_APKS="…" ./tests/test_arsc_encode.sh     # 22/22, no ASan/UBSan/LSan report
```

---

## 7. What is left

Everything from the previous handoff is done:

| # | Item | State |
|---|---|---|
| 1 | Issue #5 — the native `resources.arsc` encoder | ✅ done, §5 |
| 2 | Open an issue for the ann3/ann4 annotation defect | ✅ **#28** → fixed, released |
| 3 | Close #13, #22, #8, #7, #25, #24, #5 on GitHub | ✅ all closed with evidence |
| 4 | CHANGELOG entries | ✅ under `## [0.8.3]` |
| 5 | Version bump + tag | ✅ `6834c06` + `v0.8.3` pushed |
| 6 | Full regression before pushing | ✅ §6.3 — all green |
| 7 | Push + PR + merge | ✅ PR #29 merged as `b98d6cd` |
| 8 | Issue #28 — the annotation `.enum` writer | ✅ closed with evidence |
| 9 | Issue for the v1 (JAR) signing path | ✅ **#30** opened (§8) |

### The next pieces of work (in order)

1. **Issue #30 — the v1 (JAR) signing path** (§8): switch to SHA-256 and fix
   the zip/manifest mismatch that makes every entry a "nonexistent signed
   entry". Acceptance criteria are in the issue.
2. **An XML → ARSC compiler** (§5.3) — the natural follow-up to the encoder:
   `build_apk` copies the original `resources.arsc` back, so a value edited in
   `res/values/strings.xml` is still lost. The model and the writer exist.
3. **The Small File Rule violations** (§4.6) — `smali_writer.c` 1233 LOC,
   `smali_pool.c` 767, `smali_insn_parser.c` 526, `smali_parser.c` 512.
4. **`smali_pool.c` `comp_field_key` / `comp_method_key`** — the strict weak
   ordering concern (LOW).
5. **The 53 pre-existing warnings** (§6.4) — real, out of scope so far.
6. **Delete `fix/open-issues-13-25-24-5-22-8`** (local + remote) once the
   issue comments that cite its commits no longer need them:
   `git push origin --delete fix/open-issues-13-25-24-5-22-8`.

### The full regression, for the next run

```bash
cd /home/grandpa/me/code/c-lang/Agent-X
rm -rf .zig-cache && make clean && make release
for t in t1 t2 t3 t4 ann ann2 ann3 ann4 bulk cls big huge p2 p3 p4 mref onlymethods \
         fieldkey x_field_arrow_nocolon x_enum_qmark x_method_arrow_noparen proto70 \
         tldedup proto300 proto300b dbgcmp dbg2 b1 b2 b3 b4 b5 b6 b7 b8 ann5 ann6 ann7 r2; do
  d=/tmp/opencode/$t; [ -d "$d" ] || continue
  ./agent-x tool smali_assemble src_dir=$d out_dex=/tmp/opencode/final_$t.dex >/dev/null 2>&1 \
    && echo "OK $t" || echo "FAIL $t"
done
./tests/test_apk_signing.sh
./tests/test_smali_register_limits.sh
./tests/test_smali_diagnostics.sh
./tests/test_smali_annotations.sh
ARSC_TEST_APKS="$(ls ~/Downloads/Telegram\ Desktop/*.apk)" ./tests/test_arsc_encode.sh
python3 tests/dex_invariants.py /tmp/opencode/final_*.dex
# plus: baksmali d on the outputs, and an ASan/UBSan cold build (§6.4)
```

---

## 8. Issue #30 — the v1 (JAR) signing path. **OPEN**

Reproduced on v0.8.3 (`resign_apk … scheme=v1`, 4.4 MB real APK):

```
$ jarsigner -verify -verbose t13b.apk.signed
    Digest algorithm: SHA1 (disabled)
    Signature algorithm: SHA1withRSA (disabled), 2048-bit key
WARNING: The jar will be treated as unsigned, because it is signed with a weak algorithm ...
     Warning: nonexistent signed entries: [res/…, AndroidManifest.xml, classes.dex, …]  # every entry
     s = signature was verified
```

- **Defect 1**: SHA-1 (`SHA1-Digest` in MANIFEST.MF); must be SHA-256 — the
  code exists already in `src/crypto/sha256.c` (used by v2/v3).
- **Defect 2**: the v1 zip writer's entries do not line up with what the
  MANIFEST.MF names — likely entry-name mismatch, manifest written against a
  different entry list, or entries added after the manifest was generated.
- The apksigner `DOES NOT VERIFY` on v1-only output is *expected* (targetSdk 34
  requires v2+); the jarsigner warnings are the real defects.
- Acceptance criteria in the issue: `jarsigner -verify` clean, SHA-256, a
  regression script in `tests/`, and a combined `v1,v2,v3` output verified.

---

## 9. Environment cheat-sheet

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
