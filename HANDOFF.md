# Agent-EJ — Handoff (after the release of v0.9.0)

**Repo:** `GrandpaEJ/Agent-EJ` · **Branch:** `main` (= `origin/main`) at `21f6f89` = release **v0.9.0**, tag pushed.
**Status:** **Every open issue is closed** (#5 #7 #8 #13 #22 #24 #25 #26 #28 #30). Nothing is in progress; no branch is unpushed.

The previous handoff's "next steps" are settled:
- **v1 (JAR) signing path** (issue #30) → v0.8.4, PR #31 squash `c8dbd65`.
- **XML → ARSC compiler** (§7's big feature) → v0.9.0, PR #32 squash `921855f`. Details in §5.5.

Remaining items are §7 below.

---

## 0. Read this first

- Repo rules (`AGENTS.md`): **featwise commits**, **Small File Rule** (max 250 LOC per `.c`), C11, no new deps, memory-first, semantic versioning + `git tag` per release.
- Evidence protocol: **every claim must come from a command run in-session.**
- ⚠️ **`make clean` before every build.** Two separate traps:
  1. **The Makefile has no header dependency tracking** — `$(OBJ_DIR)/%.o: src/%.c` only. Editing a header does **not** recompile its consumers, so you can test a stale binary and chase a ghost bug. This cost a full debugging cycle on the XML compiler (the fix was in, the binary wasn't). Always `rm -rf .zig-cache && make clean && make release` after touching any `.h`.
  2. A stale object can break the link.
- ⚠️ **Never commit build artifacts** (`agent-ej`, `obj/`) — `.gitignore` covers them.
- ⚠️ **Never commit `CHANGELOG.md` edits from a subagent** — the lead edits it centrally (you are the lead: edit it yourself, featwise `docs(changelog)` on the branch, release commit opens the version section on main).
- ⚠️ **The zig cache hides compiler diagnostics.** `zig cc` replays cached objects without re-emitting warnings, so an incremental build reports "0 warnings" even when the code has 53. **Always `rm -rf .zig-cache` before counting warnings.** True baseline: **53 pre-existing warnings**, all in files this work never touched (`grep 'warning:' | grep -c` on a cold `make release`).
- ⚠️ **`jarsigner` must be invoked as `-verify` (with the dash).** A bare `verify` is parsed as the *signing* command with jarfile="verify" → jarsigner prompts `Enter Passphrase for keystore` and exits 1. This cost a debugging cycle in `tests/test_apk_v1_signing.sh`.
- ⚠️ **Don't run a build and a test in the same shell batch.** The rebuild deletes `agent-ej` mid-run and every test "fails" (39 phantom failures in one run).

### v1 signing knowledge you need before touching that code

- **Digest rule (apksigner parity, probed against build-tools 35.0.0):** SHA-256 iff `minSdkVersion >= 18`. Below 18 SHA-256 **fails** `apksigner verify` (`not supported on API Level(s) 15-17`); SHA-1 verifies at *every* level (rc=0 forced at min-sdk 24). No AndroidManifest.xml → SHA-256 (JAR-like input); unreadable manifest / missing uses-sdk → SHA-1 (Android defaults minSdk to 1). Implemented in `apk_v1_use_sha256()` (`format_apk_sign.c`).
- **Attribute names:** `SHA-256-Digest` / `SHA-256-Digest-Manifest` (apksigner's spelling). jarsigner's `SHA-1-Digest` (hyphen) is **rejected by apksigner** (`No digest for ... in META-INF/T.SF`); `SHA1-Digest` is accepted by both — that is what we write for SHA-1.
- **Section digests hash the raw stored bytes, wrapping included** — proven `RAW-MATCH` against apksigner's own wrapped output (`/tmp/opencode/sec_digest_probe.py`).
- **Manifest line wrapping:** JAR spec = 72 content bytes, continuation lines start with one space. jarsigner writes 72, apksigner 70, both cross-accept. Unwrapped long lines are rejected outright: a 613-byte `Name:` line → jarsigner `manifest line too long`, apksigner `No JAR signatures`.
- **Do not chase the combined-at-min24 `v1: false` verdict:** with v2 present and min-sdk ≥ 24 apksigner skips v1 verification — and reports `v1: false` for **its own output** too (forced `--min-sdk-version 18` → all three true). Our parity matrix matched apksigner's own output in every cell.
- **SHA-1 fixtures produce JDK warnings by design** (`treated as unsigned`, policy not defect — real jarsigner output does it too); the regression test asserts SHA-1 cases with apksigner only.
- PKCS#7: `encryptedDigest` must be exactly `key->bits/8` bytes (the v1 path hardcoded 256 → apksigner `Bad signature length: got 256 but was expecting 128` for 1024-bit keys; same fix #13 gave v2/v3).

### Resource compiler knowledge (v0.9.0) — read before touching §5.5's code

- **The invariant that keeps it honest:** decoding a table and compiling the resulting tree *without editing it* must reproduce the original bytes. Every design choice below exists to protect that; `tests/test_arsc_compile_xml.sh` asserts it on the fixture and on 16 real APKs.
- **An unchanged entry is never touched.** Patching it would append a duplicate string to the global pool (`count` 7527 → 7528) and change the table. Compare first, skip if equal.
- **Resolution goes through the model, not a resource id** — a decoded value has a type name and a configuration but no id, so `values-fr/<string name=x>` must reach the French entry.
- **The configuration qualifier is rebuilt with the decoder's own `apk_build_config_str()`**, fed a synthesised 20-byte `ResChunk_header` in front of the raw config the model carries (its own `size` field is what bounds the reads). Sharing one implementation is what makes the decoder's directory names and the compiler's match by construction. Note the trailing `snprintf(out, "-%s", buf)` is what adds the leading dash: `values-land`, not `valuesland`.
- **The entry's data type decides how its text is read**, never the text's shape. `gcm_defaultSenderId` holds numeric text and must stay a STRING (a heuristic turned it into INT_DEC, `data=1303` → `467550666`); colours keep their own type (`value=28` must stay `28`, not be normalised to `0x14`); values with no text form (dimension, fraction, dynamic reference) are **skipped**, not rewritten.
- **Duplicate keys are matched in entry order.** `AntiSplit-X.apk` names *every* entry `HEHE_BOI`; mapping all seven `<color>` elements onto the first lost six entries. The decoder emits one element per entry in index order, so the Nth element must reach the Nth entry (see `next_occurrence()` in `format_arsc_xml_parse.c` — its counter starts at 1, because occurrence 0 is handed out on creation).
- **`public.xml` is skipped** (ids only, the table already encodes them), as are self-closing markers, `<item>` children and anything whose captured text contains markup (a bag).
- **`aapt2 dump resources` needs a package with an AndroidManifest.xml to exit 0** — a zip holding only `resources.arsc` prints the dump but returns 1. And `$CMD | head; echo rc=$?` reports `head`'s status, not the tool's.

---

## 1. Issue inventory (all closed)

| # | Title | State |
|---|---|---|
| **5** | Native `resources.arsc` encoder | ✅ closed, PR #27 → `82ddf91`, v0.8.2 |
| **13** | Native v3 APK signing: RSA does not verify | ✅ closed, PR #27 |
| **22** | `smali_encoder`: no register range validation | ✅ closed, PR #27 |
| **8** | Smali Assembler — Validation & Error Reporting | ✅ closed, PR #27 |
| **7** | Smali Assembler — Debug Info | ✅ closed, PR #27 |
| **25** | `format_axml_encode`: gravity enum strings | ✅ closed, PR #27 |
| **24** | `format_apk_decode_arsc`: resource qualifier parsing | ✅ closed, PR #27 |
| **26** | DEX 037 support, smali paths, hardening | ✅ closed, PR #26 |
| **28** | annotation `.enum` out-of-range field index | ✅ closed, PR #29 → `b98d6cd`, v0.8.3 |
| **30** | v1 JAR signing: SHA-1 + `nonexistent signed entries` | ✅ closed, PR #31 → `c8dbd65`, v0.8.4 — §3 |

v0.9.0 closed no issue: it shipped the handoff's designated next feature (§5.5).

## 2. Commits

### v0.9.0 (XML → ARSC compiler) — squash merge `921855f` on main, release `21f6f89`

```
21f6f89 release: bump version to 0.9.0
921855f feat(arsc): compile the decoded res values tree back into resources.arsc (XML → ARSC) (#32)
```
Branch (deleted on merge), in order:
```
6e0d9ca docs(changelog): record the res values XML compiler
1e74c2e test(arsc): regression tests for the XML resource compiler
87baeb5 feat(apk): recompile the resource table during build_apk
fb06314 feat(tools): add the arsc_compile tool
5c1732d feat(arsc): compile the decoded res values tree back into resources.arsc
```

### v0.8.4 (issue #30) — squash merge `c8dbd65`, release `228ca91`

```
228ca91 release: bump version to 0.8.4
c8dbd65 fix(apk): v1 signing — apksigner-parity digest, META-INF preservation, long-name safety (#30) (#31)
```
Branch (deleted), in order: `ef20be1` docs · `ec0a4c4` test · `a56a8eb` PKCS#7 key-length · `4e1dd2a` wrap + growable buffers · `5eee8d3` apksigner-parity digest · `c0642af` META-INF content · `4ffa073` split the v1 signer

### Earlier (from the v0.8.3 handoff)

```
4cc7bf9 docs(handoff): record the v0.8.3 release and issue #30   (superseded by this file)
6834c06 release: bump version to 0.8.3
b98d6cd fix(smali): annotation .enum fixes (#28) (#29)
3dbd0ff release: bump version to 0.8.2
82ddf91 feat: arsc encoder, signed APK fix, smali hardening (#27)
```

## 3. Issue #30 — the v1 (JAR) signing path. **FIXED, VERIFIED, RELEASED (v0.8.4)**

### 3.1 Four defects, each with a control experiment

1. **SHA-1 digest, disabled by modern JDKs** (the reported symptom). Control: real jarsigner's *own* SHA-1 output reproduces both warnings (`treated as unsigned` + `nonexistent signed entries: [f.txt]`) on a trivial jar; its SHA-256 output verifies clean. The entry names matched zip↔manifest↔SF **100/100** all along — the warning was the JDK's disabled-algorithm path, not a zip defect. Fix: the apksigner-parity digest rule (§0 table), attribute names `SHA-256-Digest`/`SHA1-Digest`, PKCS#7 `rsa_sign_sha256` + OID `2.16.840.1.101.3.4.2.1` in SignedData *and* SignerInfo (`openssl pkcs7 -print` confirms `sha256WithRSAEncryption`).
2. **META-INF content dropped** (found while investigating #1): the signer skipped *every* `META-INF/` entry — the test APK lost **71 content files** (androidx `.version`, `services/`, build metadata) from the output and from the manifest. Both reference signers keep and sign them (their manifests list `META-INF/...`). Fix: `apk_is_signature_file()` mirrors JDK `SignatureFileVerifier.isSigningRelated()` (directly under META-INF/: MANIFEST.MF, .SF/.DSA/.RSA/.EC, SIG-* with1-3 alnum ext); content files are signed like any entry, in the manifest *and* in the copy loop.
3. **Long entry names → unverifiable output** (pre-existing): unwrapped `Name:` lines — 613 bytes → jarsigner `manifest line too long` **and** apksigner `No JAR signatures` (both reference tools reject). The fixed `256+n*256` buffer let `*out_len` pass the allocation (820 for 768 → the SF hashed heap garbage), and `name_line[512]` silently dropped long names. Fix: both builders append into the in-tree `arsc_buf`; name lines wrap at 72 content bytes (continuation = one leading space); the SF copies the manifest's name lines **byte-for-byte** so wrapped names stay wrapped and logical names match.
4. **PKCS#7 `encryptedDigest` hardcoded 256**: non-2048-bit keys → broken CERT.RSA (apksigner: `Bad signature length: got 256 but was expecting 128`; jarsigner: `not parsable or not verifiable`). Fix: `key->bits/8` — the fix #13 already gave v2/v3.

### 3.2 Parity matrix (our output vs apksigner's own, same fixtures)

| fixture | ours | apksigner's own |
|---|---|---|
| min24, v1-only | v1 true | v1 true |
| min17, v1-only, SHA-1 | apksigner verifies | same choice, same verdict |
| min18, v1+v2+v3 | all three true | all three true |
| min24, v1+v2+v3 | v1 false, v2/v3 true | **identical quirk** (forced min18 → all true) |
| no uses-sdk | SHA-1, verifies | SHA-1, verifies |

### 3.3 Long-name evidence (before → after)

| fixture | max `Name:` line | jarsigner | apksigner |
|---|---|---|---|
| 342-byte name, old code | 348 (unwrapped) | tolerated | tolerated |
| 613-byte name, old code | 619 | ✗ `manifest line too long` | ✗ `No JAR signatures` |
| 613-byte name, new code | **72** | ✗→**`jar verified.`** | ✗→**rc=0** |
| 542-byte name, new code (test) | **72** | `jar verified.` | verifies |

Section digests on wrapped output: `RAW-MATCH` (hash of raw stored bytes) — probe script `/tmp/opencode/sec_digest_probe.py` (ephemeral; recreate from §6 if needed).

## 4. Issues #5/#7/#8/#13/#22/#24/#25/#26/#28 — closed (see the v0.8.2/v0.8.3 handoffs for the full detail)

Carried summaries:
- **#13 APK signing**: cert/key mismatch root cause; `der.c` SPKI; both paths refuse mismatched certs (`cert_key_mismatch`, exit 1); `resolve_default_key_path()`; `tests/test_apk_signing.sh` **12/12**.
- **#22/#8/#7 smali**: 27-row register limit table (`smali_validate.c`); `file:line:` diagnostics; type-list dedup + growable param buffer; debug info uleb128p1/section fixes; `tests/test_smali_register_limits.sh` **21/21**, `tests/test_smali_diagnostics.sh` **7/7**.
- **#28 annotations**: `.enum` through the field pool, malformed `.enum` rejection, binding only at `.end field`, use-after-free in the class parser; module `smali_annot.c`; `tests/test_smali_annotations.sh` **16/16**.
- **#24/#25 apks**: full `ResTable_config`; gravity enum strings.
- **#5 arsc encoder**: model/load/write pipeline; 17 real APKs byte-identical round trip; `tests/test_arsc_encode.sh` **22/22**.
- **#26**: DEX 037 allowlist, descriptor path fix, G7/G15, multi-DEX assembly.

Known-not-fixed in those areas (carried): `smali_pool.c` `comp_field_key`/`comp_method_key` strict-weak-ordering (LOW, from the PR review).

## 5. Issue #5 — Native `resources.arsc` encoder (the base for the XML→ARSC compiler)

### 5.1 What was there before

`src/android/arsc/format_arsc_build.c` had `arsc_build()`, but it only **patched the
global string pool** and copied the rest of the original file byte for byte.
It was not a resource table writer, and the old parser kept only the **last**
configuration of every type, so the other configurations were unreachable.

### 5.2 The shipped code

| File | LOC | What it does |
|---|---|---|
| `format_arsc_model.h/.c` | 122 / 211 | the model: global value pool, packages, types, variants, entries, map items + `arsc_model_set_string()` / `arsc_model_set_int()` |
| `format_arsc_load.c` | 186 | ARSC → model (string pool, package, type spec, type chunk, entries) |
| `format_arsc_pool.c/.h` | 48 | string pool reader, UTF-16 package name |
| `format_arsc_fmt.h` | 116 | little-endian accessors, UTF-8 ↔ UTF-16 |
| `format_arsc_buf.c/.h` | 83 | the growable byte buffer of the writer (**reused by the v1 signer since v0.8.4**) |
| `format_arsc_write.c` | 94 | `arsc_encode()`: table, package chunks |
| `format_arsc_write_pool.c` | 78 | the string pools |
| `format_arsc_write_entry.c` | 96 | type spec, type chunk, entries |
| `src/tools/tool_arsc.c` | 218 | the `arsc_encode`, `arsc_info` and `arsc_compile` tools |

The old `format_arsc_parse.c` / `format_arsc_build.c` are untouched: the AXML
decoder still uses them, the encoder is a second, independent path.

### 5.3 The format details that cost the most time (memorize before touching ARSC)

- **`ResTable_map` is 12 bytes, not 8**: a map item is a `name` (u32) plus a full
  `Res_value` (size, res0, dataType, data). Reading it as 8 bytes shifts every
  complex entry and loses 6,576 bytes in the `attr` type of the test APK.
- **The type spec carries `res1`**: aapt2 stores the config flags of the type in the
  `uint16` at offset 10 of the `ResTable_typeSpec` chunk (0x0001 for `anim`,
  0x0057 for `string`, …). It is not padding — dropping it changes 16 bytes of the
  test APK.
- **Entry offsets are relative to `entriesStart`**, not to the chunk start.
- **The package name is UTF-16** and must be decoded to UTF-8 and back; a
  one-byte-per-code-unit copy leaves the tail of the buffer uninitialised and
  writes garbage after the terminator (this broke 6 of the 17 test APKs).
- **The package id is a `uint32`**, not a byte — writing one byte shifts the whole
  package header.

### 5.4 Verification

```
17 real APKs round tripped      → 17/17 byte identical (cmp, not a dump)
GA Notification Monitor.apk    → 1,024,736 bytes in, 1,024,736 bytes out, cmp clean
independent oracle dump        → 21,814 lines, 0 differences
fixture from tests/arsc_build  → byte identical
patched APK                    → aapt2: string/app_name = "Agent-EJ Patched"
                                 apktool: <string name="app_name">Agent-EJ Patched</string>
```
`tests/test_arsc_encode.sh` (**22/22**): takes the binary path as its first argument
and the APKs from `ARSC_TEST_APKS` (newline separated). `tests/arsc_dump.py` is the
independent oracle, `tests/arsc_build.py` builds a fixture without aapt2.

### 5.5 The XML → ARSC compiler — **SHIPPED in v0.9.0** (this was §7's item 2)

`decode_apk` wrote the resources as XML and `build_apk` copied the original
`resources.arsc` back, so a value edited in `res/values/strings.xml` was lost. The
compiler closes the loop.

| File | LOC | What it does |
|---|---|---|
| `format_arsc_xml.c` | 114 | `arsc_compile_xml()`: walk the values directories, encode, write |
| `format_arsc_xml_entry.c` | 164 | qualifier rebuild, entry lookup, type-driven value conversion |
| `format_arsc_xml_parse.c` | 207 | element scanner, entity unescaping, duplicate-key occurrence counting |
| `format_arsc_xml_internal.h` | 36 | the module-private API between those three |
| `format_apk_build_res.c` | 47 | `apk_recompile_resources()`: the `build_apk` hook |
| `tests/test_arsc_compile_xml.sh` | 294 | 53 assertions |
| `tests/arsc_xml_probe.py` | 42 | isolates the dump lines naming one key |

Design rules and the defects that forced them are in §0 ("Resource compiler
knowledge"). In short:

```
unedited round trip      → byte-identical on the fixture + 16/16 real APKs
edits proven            → string (pool +1), bool (type 18 kept), colour (type 0x1c kept),
                           integer, qualified values-land (default config untouched)
end to end              → decode BotX Dialog → edit app_name → build_apk
                          aapt2: string/app_name = "BOTX_FULL_E2E_OK", badging parses,
                          apksigner verify rc=0
build_apk hook          → compiles to a temp file renamed over the original; on failure
                          the original table is packed (old behaviour, never a broken build)
```

Three defects were found *by* the byte-identity assertion and fixed: duplicate keys
collapsed onto the first entry (6 of 7 colours lost in `AntiSplit-X.apk`), a
number-shaped string promoted to an integer, and colour types normalised to `0x14`
(250 entries of GA Notification Monitor).

## 6. Test assets (all in the repository)

| Script | What it does | Result (v0.9.0) |
|---|---|---|
| **`tests/test_arsc_compile_xml.sh`** (`ARSC_TEST_APKS`) | XML→ARSC: unedited byte-identity on fixture + all given APKs, single-edit isolation, bags/unknown types untouched, `build_apk` packs the compiled table, qualified-directory edits spare the default config, aapt2 accepts | **53/53** |
| `tests/test_apk_v1_signing.sh` | issue #30: digest rule at the minSdk 17/18 boundary, META-INF preservation, combined v1+v2+v3 (both oracles), a 542-byte wrapped name, a 1024-bit key | **28/28** |
| `tests/test_apk_signing.sh` | issue #13: cert/key mismatch + apksigner v2/v3 | **12/12** |
| register/diagnostics/annotations suites | issues #22/#8/#28 | **21/21 · 7/7 · 16/16** |
| `tests/test_arsc_encode.sh` (`ARSC_TEST_APKS`) | issue #5 | **22/22** |
| `tests/dex_invariants.py` · `arsc_dump.py` · `arsc_build.py` · `arsc_xml_probe.py` | G7/G8/header checks; ARSC oracle; fixture builder; key-line isolator | 0 failures |

Every script takes the binary path as `$1` (default `./agent-ej`).

### Current regression status (v0.9.0, cold `make release`)

```
test_arsc_compile_xml.sh      → 53 pass, 0 fail      (16 APKs: round trip + aapt2)
test_apk_v1_signing.sh        → 28 pass, 0 fail      (oracles: apksigner 35.0.0, JDK 21 jarsigner)
test_apk_signing.sh           → 12 pass, 0 fail
test_smali_register_limits.sh → 21 pass, 0 fail
test_smali_diagnostics.sh     → 7 pass, 0 fail
test_smali_annotations.sh     → 16 pass, 0 fail
test_arsc_encode.sh           → 22 pass, 0 fail       (159 total assertions across the 7 suites)
39 smali fixtures assemble    → 39 OK, 0 FAIL
baksmali on all outputs       → all OK
python3 tests/dex_invariants.py → 0 failures
ASan/UBSan/LSan (compiler, encoder, build/decode, signing paths) → no report
warnings (rm -rf .zig-cache)  → 53 (unchanged baseline)
byte-identity                 → 16/16 real APKs re-encode byte for byte through the compiler;
                                short-name fixtures identical across the v1 refactor
```

### Full regression recipe

```bash
cd /home/grandpa/me/code/c-lang/Agent-EJ
rm -rf .zig-cache && make clean && make release          # never parallel with a test run
for t in t1 t2 t3 t4 ann ann2 ann3 ann4 bulk cls big huge p2 p3 p4 mref onlymethods \
         fieldkey x_field_arrow_nocolon x_enum_qmark x_method_arrow_noparen proto70 \
         tldedup proto300 proto300b dbgcmp dbg2 b1 b2 b3 b4 b5 b6 b7 b8 ann5 ann6 ann7 r2; do
  d=/tmp/opencode/$t; [ -d "$d" ] || continue
  ./agent-ej tool smali_assemble src_dir=$d out_dex=/tmp/opencode/final_$t.dex >/dev/null 2>&1 \
    && echo "OK $t" || echo "FAIL $t"
done
APKS="$(ls ~/Downloads/Telegram\ Desktop/*.apk)"
./tests/test_arsc_compile_xml.sh                       # or ARSC_TEST_APKS="$APKS" ...
./tests/test_apk_v1_signing.sh
./tests/test_apk_signing.sh
./tests/test_smali_register_limits.sh
./tests/test_smali_diagnostics.sh
./tests/test_smali_annotations.sh
ARSC_TEST_APKS="$APKS" ./tests/test_arsc_encode.sh
python3 tests/dex_invariants.py /tmp/opencode/final_*.dex
/home/grandpa/.local/bin/baksmali d /tmp/opencode/final_t1.dex -o /tmp/opencode/final_t1_out
```
Sanitizer pass:
```bash
rm -rf .zig-cache && make clean && make CC="gcc -fsanitize=address,undefined -fno-omit-frame-pointer -g" \
                                            LDFLAGS="-fsanitize=address,undefined"
ASAN_OPTIONS=detect_leaks=1 ./tests/test_arsc_compile_xml.sh
ASAN_OPTIONS=detect_leaks=1 ./tests/test_arsc_encode.sh
ASAN_OPTIONS=detect_leaks=1 ./tests/test_apk_signing.sh
```
Then rebuild `make release` for the real binary. Warning count only from a cold cache.

### Ephemeral fixtures in `/tmp/opencode` (recreate if gone)

- Signing probes for #30: `f15` (min15), `f00` (no uses-sdk), `f18t23` (min18), `f24t23`/`f24clean` (min24), `longname` (342-byte name), `huge` (613), `noman` (manifest-less) — each built with `agent-ej tool axml_assemble` + `zip -qr`, signed with `resign_apk key=/tmp/opencode/testkey_p8.pem cert=/tmp/opencode/testcert.der`.
- Resource probes: any decode works, e.g. `agent-ej tool decode_apk path="…/GA Notification Monitor.apk" out_dir=/tmp/opencode/dx` (its `values-land/integers.xml` holds `mtrl_calendar_header_orientation`, whose default value differs — the qualified-config fixture). `AntiSplit-X.apk` is the duplicate-key table.
- `sec_digest_probe.py` — proves the section-digest convention (RAW vs UNWRAPPED).
- Test material: `openssl genrsa -traditional -out testkey.pem 2048` → `openssl pkcs8 -topk8 -nocrypt …` → `openssl req -new -x509 … -outform DER -out testcert.der` (the tool parses **PKCS#8** only).

## 7. What is left

| # | Item | State |
|---|---|---|
| 1 | Issue #30 — v1 signing path | ✅ **done, v0.8.4** |
| 2 | **XML → ARSC compiler** | ✅ **done, v0.9.0** — §5.5 |
| 3 | **Small File Rule violations — 20 `.c` files over 250 LOC**, not 4. Worst: `smali_writer.c` 1233, `smali_pool.c` 767, `format_dex_smali_method.c` 689, `format_axml_encode.c` 583, `smali_insn_parser.c` 526, `smali_parser.c` 508, `format_dex_disasm.c` 420, `agent.c` 387, `format_axml_output.c` 364, `format_arsc_toml.c` 347, `tool_dex.c` 333, `format_apk_decode_arsc.c` 327, `format_apk_build.c` 305 (grew +4 in v0.9.0; the compiler's hook was deliberately put in its own 47-line file to stop it growing). Full list: `find src -name '*.c' \| xargs wc -l \| sort -rn \| awk '$1>250'` | open |
| 4 | `smali_pool.c` `comp_field_key`/`comp_method_key` strict-weak-ordering (LOW, PR review finding) | open |
| 5 | The 53 pre-existing warnings (files untouched so far) | open |
| 6 | Delete the kept branch: `git push origin --delete fix/open-issues-13-25-24-5-22-8` (its commits are cited in issue comments; decide when those comments no longer need them) | open |

Suggested order: **3 / 4 / 5** as refactors (no open feature work remains), 6 whenever.
The only feature-shaped gap left in the APK pipeline is the reverse of §5.5: the
decoder's *file* side (it writes `res/<type>/…` summaries for non-value types and
packs the whole `res/` tree of raw XML into the built APK) — out of scope for
v0.9.0, worth an issue if it ever matters.

## 8. Environment cheat-sheet

```bash
# build (cold cache for real warnings)
cd /home/grandpa/me/code/c-lang/Agent-EJ && rm -rf .zig-cache && make clean && make release

# oracles
/home/grandpa/Android/Sdk/build-tools/35.0.0/apksigner    # APK oracle (v1 digest probes, verify)
/usr/bin/jarsigner                                         # JDK 21 JAR oracle — always `-verify`
/home/grandpa/.local/bin/baksmali                          # reference disassembler
/home/grandpa/.local/bin/smali                             # reference assembler
/home/grandpa/Android/Sdk/build-tools/35.0.0/aapt2         # ARSC/APK oracle (needs a manifest in the zip)
/home/grandpa/.local/bin/apktool                           # APK decode oracle

# key material (the tool parses PKCS#8, not PKCS#1)
cd /tmp/opencode
openssl genrsa -traditional -out testkey.pem 2048
openssl pkcs8 -topk8 -nocrypt -in testkey.pem -out testkey_p8.pem
openssl req -new -x509 -key testkey.pem -subj "/CN=Test" -days 3650 -outform DER -out testcert.der

# sample APKs
~/Downloads/Telegram\ Desktop/GA\ Notification\ Monitor.apk   # rich qualifiers; qualified-edit fixture
~/Downloads/Telegram\ Desktop/AntiSplit-X.apk                 # every entry named HEHE_BOI (duplicate keys)
~/Downloads/Telegram\ Desktop/BotX\ Dialog.apk                # small APK whose build round trips end to end
~/Downloads/Telegram\ Desktop/base\ (9).apk                   # 4.8 MB resource table
~/me/code/rust/ZeroBot/waifuhost-apk/app/build/outputs/apk/release/app-release-unsigned.apk
```

**DEX constraints used throughout:** G7 (size 0 ⇒ offset 0), G8 (4-byte alignment), G15 (`utf16_size` matches decoded length).
