# Changelog

All notable changes to the Agent EJ project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

---

## [Unreleased]

## [0.12.0] - 2026-10-29

### Added
- **Native signature killer (`sigkill`)** — MT/NP-Manager-style APK signature-verification bypass implemented entirely in C, with nine independent strategies selectable per run: `scan` (detect signature-check methods and hardcoded certificate digests), `stub` (neutralise boolean/int/String/`Signature`-returning check methods while preserving annotations), `pkg` (inject `com.agent.ej.SigKiller` and hook every `PackageInfo.signatures` read plus `SigningInfo.getApkContentsSigners()` / `getSigningCertificateHistory()` so the app keeps seeing the original certificate), `call` (retarget `PackageManager.getPackageInfo` / `getPackageArchiveInfo` / `getInstalledPackages` invocations to a static wrapper that returns an already-fixed object), `sigobj` (override `Signature.toCharsString()` / `hashCode()` / `equals()` results at their call sites with the original certificate's values and retarget `toByteArray()` to the original DER), `checkapi` (force `PackageManager.checkSignatures(...)` to return `0`, `SIGNATURE_MATCH`), `zipcert` (copy the original `META-INF` signature block to `assets/sigkill/` and retarget code that opens `META-INF/CERT.RSA` in its own APK to that copy), `const` (rewrite hardcoded DER-hex/`toCharsString`, MD5, SHA-1, SHA-256, base64 and Java `hashCode` values from the original certificate to the one the APK is signed with), and `native` (patch the same digest strings in `lib/**/*.so` and `assets/**` in place). The original certificate is extracted natively from the APK's `META-INF/*.RSA` PKCS#7 block or supplied as DER/PEM. Handles both APK input (decode → patch → rebuild → sign) and decoded trees, and normalises PEM certificates to DER for the signer. Runtime-original strategies (`pkg`/`call`/`sigobj`/`checkapi`/`zipcert`) and static-rewrite strategies (`const`/`native`) are mutually exclusive and are resolved automatically. Adds `include/sigkill.h`, `src/android/sigkill/` (twelve small modules) and the `sigkill`/`signature_killer`/`sig_killer` tool aliases.
- **Deobfuscation tool (`deobfuscate` / `deobf`)** with three modes over DEX, binary XML and resource tables. **repair** undoes anti-decompile tampering so jadx/apktool/aapt stop crashing: DEX header normalisation (`header_size`, `endian_tag`, `file_size`) with recomputed Adler-32 checksum and SHA-1 signature (`dex_repair`), binary-XML root/sub-chunk size lies corrected (`axml_repair`), and junk-decorated `resources.arsc` normalised by a model round-trip (`arsc_repair`); runs on a standalone `.dex`/`.xml`/`.arsc` or APK-wide, repairing every entry and re-signing (`apk_deobf_repair`). **map** applies an R8/ProGuard `mapping.txt` to a decoded smali tree, restoring original class, field and method names at both declaration and reference sites, keyed by owning class, and leaving ambiguous overload-collapsed names untouched. **heuristic** needs no mapping: it detects obfuscated names (non-printable/invisible bytes, or ProGuard one/two-letter names) and assigns stable, deterministic readable names (`deobf/ClassNNNN`, `mNNN`/`fNNN`) consistent across the whole tree. map/heuristic rename for analysis/readability only and are not meant to be rebuilt into a running APK. Adds `src/android/{dex,axml,arsc}/format_*_repair.c`, `src/android/apk/format_apk_deobf.c` and `src/android/smali/deobf_{table,rewrite,map,heur}.c`. Verified: a tampered APK repairs to a byte-identical valid DEX and a parseable manifest and re-signs cleanly; a real app with 167 invisible-Unicode class names deobfuscates to zero remaining obfuscated bytes with consistent references throughout.


### Fixed
- **Fixed the tool schema JSON** — three missing commas (`arsc_encode`, `arsc_info`, `arsc_compile`) made `cJSON_Parse` reject the whole schema, so `tools`/`tools_get_definitions` returned nothing to the LLM. The schema now parses and lists all 31 built-in tools, including `sigkill`.


## [0.11.0] - 2026-10-29

### Added
- **Native anti-split APK merge (`merge_apk` / `antisplit`)** — merges a split APK set (a directory of APKs, or an `.xapk`/`.apks`/`.apkm` bundle) into one standalone, installable APK, entirely in C with no external tool. Unions the base with its configuration splits: folds every split's `resources.arsc` into the base table (full table union across density/language/ABI variants, with lazy string/key remapping and OR'd spec flags — `arsc_model_merge`), copies each split's native libraries, drawables and locale assets (base wins any path collision), and clears the base manifest's split requirements (`requiredSplitTypes` emptied, `isSplitRequired` forced false — `apk_fix_split_manifest`) so PackageManager installs it alone. Entries are stored 4-byte aligned (4 KiB for `*.so`) and the result is signed through the shared v1→zipalign→v2/v3 pipeline (`apk_sign_pipeline`, factored out of `apk_build`). Adds `src/android/apk/format_apk_merge*.c`, `format_apk_manifest_fix.c`, `format_apk_sign_pipeline.c` and `src/android/arsc/format_arsc_merge.c`. Verified on real split bundles: merged tables carry every configuration with zero aapt2 errors, and a merged no-native-library app installs standalone and runs on an emulator.


## [0.10.0] - 2026-10-29

### Fixed
- **Reference resolution picked the wrong resource in obfuscated tables** — `arsc_lookup_id()` returned the first entry whose key matched, but name-obfuscated apps give many entries the same key (e.g. every string named `HEHE_BOI`). A manifest `@string` reference then decoded to a name that re-encoded to a *different* entry, so a rebuilt AntiSplit-X showed `android:label` as "About" instead of its real title. The lookup now verifies the name maps back to the same resource ID and returns NULL (printing `@ref/0x…`) when it does not, keeping the numeric ID exact through a round trip.
- **XML text content was silently dropped when encoding binary XML** — the text parser skipped everything between tags, so `<item>Hello</item>` was rebuilt as `<item />`. Text runs (entity-decoded, trailing indentation trimmed, whitespace-only runs ignored) and `<![CDATA[...]]>` sections now become text nodes and are written as `RES_XML_CDATA` chunks, in document order alongside child elements.
- **Decoded XML repeated `xmlns:android="…"` on every element** — the decoder never marked a namespace as already declared, so each element re-printed every namespace in scope. Each declaration now appears once, on the element it was declared on.
- **Opcode `0xD1` was assembled and disassembled as `rsub-int/lit16`, but every real compiler/toolchain (`d8`, `dx`, `baksmali`, `smali`) spells it `rsub-int`** — the one case in the `*-int/lit16` family (0xD0-0xD7) where the historical mnemonic dropped the format suffix (0xD9, `rsub-int/lit8`, is unaffected and keeps its suffix). Because both agent-ej's disassembler and assembler shared the same wrong spelling, a pure agent-ej-to-agent-ej round trip never surfaced it — it only broke when assembling real `baksmali.jar` output or any smali written against the standard toolchain, both extremely common since `rsub-int` compiles from ordinary reversed-operand subtraction (`length - i`, `n - 1`, …). Found while benchmarking `smali_assemble` against `smali.jar` on `baksmali.jar`'s own output for a real 7,193-class `classes.dex` — the assembly failed outright with `unknown instruction 'rsub-int'`. Fixed in both `smali_optab.h` and `format_dex_disasm.c`; verified the disassembled mnemonic is now byte-identical to `baksmali.jar`'s. No regressions (60/60 existing assertions still pass).


- **A field with two or more `.annotation` blocks before its `.end field` lost everything after the first one.** `smali_parser.c`'s field-body parser decided "field-owned vs. class-level" by peeking only at the single token pair right after one annotation's `.end annotation`; a second annotation on the same field (e.g. a `@GuardedBy` build annotation followed by the compiler's generic `Ldalvik/annotation/Signature;`) made that peek see another `.annotation`, not `.end field`, so it handed the *first* annotation to the class and abandoned the field body mid-parse — leaving the second annotation's text and the real `.end field` to be misread as top-level class directives (`unknown directive '.end' at class level`). Found stress-testing a real modded WhatsApp APK: **over 500 classes across androidx, okhttp and Firebase failed to reassemble** from this one pattern, which is routine in any AndroidX-linked app. The parser now collects a whole run of consecutive field-level `.annotation` blocks before deciding where it belongs.
- **A `.param` debug name containing `"` or `\` was written unescaped**, producing unparseable smali such as `.param p1, "style="dashed""` — seen in the same APK, apparently a real-world case of an obfuscator salting debug names specifically to break naive disassemblers. `format_dex_smali_method.c` now runs `.param` names through the same MUTF-8-aware escaper (`smali_escape_str`, now shared via `format_dex_internal.h`) already used for every other string in the disassembler.
- **`smali_assemble` wrote a corrupt `type_ids` entry (`0xFFFFFFFF`) for a type that appears only as a `.array` element inside an annotation value** (e.g. a `Ldalvik/annotation/Throws;`-style `value = {Lcom/x/A;, Lcom/x/B;}` on a method, common in reflection-heavy obfuscated code). `pool_add_annotation_elem()`'s `VALUE_TYPE_TYPE` branch, and its duplicate in the method-annotation array walk, added the type string to the type pool but never to the string pool that `smali_writer.c` resolves `type_ids` against, so the lookup silently returned `-1`. The resulting DEX passed `smali_assemble` with no error but **segfaulted any later `disasm_dex`/`decode_apk` of that output** (`heap-buffer-overflow` in `smali_res()`, confirmed under ASan) and **silently dropped the last 12 classes** written after the corrupt entry. Both call sites now add the type to the string pool alongside the type pool, matching every other `VALUE_TYPE_*` branch.
- **Every disassembled `.param` name was off by one string-pool index** (`format_dex_smali_method.c` read the raw `uleb128p1` debug value and used it directly as a string index, instead of subtracting 1 — 0 means "no name", not index 0). On a shared string pool this silently attached the *next* string after the real one to every parameter, found only once the stress harness started comparing full file content instead of just class counts.
- **The smali *assembler* lost which register a `.param` name belonged to.** `.param pN, "..."` was appended to a packed array in the order the directives appeared in the file, discarding `N` entirely; a method whose first parameters have no debug name (very common — a leading `this` or an unnamed synthetic arg) reassembled with every later name shifted onto the wrong, lower-numbered register. The parser now computes each name's real slot from its register number relative to the method's first "in" register. The disassembler's own `.param p%d` printer had the mirror bug — it printed the *array index* as the register number instead of adding that same offset back — fixed alongside it.
- **`.line 0` was silently dropped from debug info**, both read and write: `format_dex_smali_method.c`'s bytecode-address loop and `smali_debug.c`'s line-search/emission logic each treated line number `0` as "no line info" (`<= 0` / `> 0`), when D8 legitimately emits line `0` for synthetic lambdas and bridge methods. Both now treat only `-1` (unset) as absent, so line `0` round-trips like any other line number.
- **`decode_apk` silently dropped and overwrote classes whose own simple name contains an uppercase `L`** — `Logger`, `Layout`, `Loader`, `List`, `Lock`, `Link`, `Level`, `Locale`, `Long`, … extremely common. The per-class `.smali` output path was built with `strrchr(class_line, 'L')` to find where the type descriptor starts, but that finds the *last* `L` in the whole `.class` line, not the package-leading one; when the class's own name contained a later `L`, the path was truncated to whatever followed it, so e.g. `Lcom/my/newproject/SketchLogger` and `Lorg/apache/commons/logging/impl/Jdk14Logger` both wrote to `ogger.smali` — the second class **silently overwrote the first on disk**. On one real multidex APK this alone **dropped 97% of its classes** (7,475 → 202) without any error. Now finds the descriptor as the line's last whitespace-separated token instead.
- Combined, all seven fixes above were found and verified by stress-testing real-world APKs with a harness that diffs the **full disassembled text** against the original decode, not just class counts — a count-only check had passed every one of these cases, including the class-overwriting one: `decode_apk` undercounts identically every time it is run against the same (buggy) binary, so a count taken before the fix always matched a count taken after, with no way to tell either was wrong. Only comparing file *content* — two completely unrelated classes coming out of the same path — exposed it. `tests/test_smali_annotations.sh` and the other existing suites still pass throughout (16/16 and no regressions elsewhere).
- **`build_apk` wrote a `code_item` whose on-disk `ins_size`/`registers_size` were garbage for some real obfuscated APKs** (`Failure to verify dex file: ins_size (NNNNd) > registers_size (Nd)`, `java.lang.ClassNotFoundException` on the app's own `Application` class, blocking install/launch entirely). Root cause, found by instrumenting `write_code_item` and confirming the in-memory value was *always* correct right up to the write (so earlier suspicion of `calculate_ins_count` in `smali_parser.c` was a dead end): `write_code_item()` wrote each method's `debug_info_item` immediately after that method's own `code_item`, interleaving `[code_A][debug_A][code_B][debug_B]…` in the file — but the DEX `map_list` declares `TYPE_CODE_ITEM` as one unbroken run of `n` consecutive items, and a conformant reader (including Android's own DexFileVerifier) finds item *N+1* purely by stepping past item *N*'s own self-described length, with no notion of the debug bytes in between. It walks straight into `debug_A`'s ULEB128 stream and misreads it as method B's `registers_size`/`ins_size` header — explaining why the corrupted values looked like plausible-but-wrong small/huge numbers, not obviously-malformed ones. Fixed by moving all `debug_info_item` writes to a dedicated pass in `smali_writer.c`, run only after every class's code items are fully written, patching each `code_item`'s `debug_info_off` slot (always at `code_off + 8`) once the real item lands.
- **That same debug-info pass then broke in a second, more subtle way**: `write_debug_info()` (`smali_debug.c`) called `align_4()` before writing, which was harmless when debug items were scattered between code items (already after code's own alignment) but, once packed back-to-back in their own section, opened a padding gap *between* debug items — bytes a `debug_info_item` reader isn't expecting, since (unlike `code_item`) it has no alignment requirement and is located purely by walking each item's self-terminating opcode stream. This surfaced as `Failure to verify dex file: Non-zero padding Nd before section of type 8192 at offset ...` right after the first fix landed — the verifier's cursor, walking items with zero expected gap, landed mid-way through the next item's real bytes and found non-zero content where it expected either more item or clean padding. Fixed by dropping the leading `align_4()` so debug items pack with no gap, matching how a reader actually consumes them.
- Both fixes verified end-to-end on the real device, not just by re-parsing the output: reinstalling the previously-crashing APK on an actual Android (Waydroid) target advanced past each error in turn as the corresponding fix landed, and the full existing suite (60/60 relevant assertions) still passes with no regressions.
- **Field/method names were escaped like string literals in the disassembly, so `build_apk` wrote them back with literal backslashes** (`Failure to verify dex file: Invalid field name: '⁭⁫'` on install). `smali_res()` (`format_dex_smali_util.c`) resolved the name of every field and method *reference* (`Lcls;->name:T`, `Lcls;->name(...)R`) and every annotation element name through the string-literal path, which turns anything outside 0x20–0x7e into `\uXXXX`. Definitions (`.field`, `.method`) were already raw, so an obfuscated member such as U+206D U+206B was defined as `⁭⁫` but referenced as `\u206d\u206b`; the assembler only decodes escapes inside quoted strings, so the reference became a second, 12-byte name containing `\` — which ART's `IsValidMemberName` rejects. Names are identifiers, not literals, and baksmali prints them raw too: added a raw member-name kind to `smali_res()` and used it for field/method refs and annotation element names. This also fixes the earlier-noted `\\u02c6` double-escaping of single-codepoint Unicode names on a second round trip, which had the same cause. Verified on the device: a fresh `decode_apk` → `build_apk` of the obfuscated APK now passes DEX verification and starts executing app code.
- **The binary-XML (AXML) encoder wrote every non-ASCII character as several garbage UTF-16 units**, so `AndroidManifest.xml` and layouts lost any non-ASCII class name (`ClassNotFoundException: com.tilks.arsc.main.￢ﾁﾭ￢ﾁﾫ…` on launch). `write_string_utf16()` (`format_axml_encode.c`) cast each UTF-8 *byte* (a signed `char`) straight to a UTF-16 unit, so U+206D (`E2 81 AD`) became `FFE2 FF81 FFAD`, and the string-offset table assumed `strlen*2`. It now decodes UTF-8 properly (surrogate pairs for supplementary characters, the long length form above 0x7FFF units) and sizes offsets from the real unit count. Because `decode_apk` re-encodes the manifest to binary at decode time, trees decoded with an older binary keep the broken manifest and have to be decoded again. Verified on an Android 7 emulator: the rebuilt manifest's class names now match the DEX.
- **Small ZIP entries were silently dropped on decode.** The inflater's fixed-Huffman literal table (`format_zip_inflate.c`) had 256 initializers instead of 288 (80 nine-bit codes instead of 112, the 7-bit codes in the wrong slots), so every fixed-Huffman block failed, which is what deflate picks for small files. 15 of 16 such entries in a real APK vanished, including `META-INF/services/*` (kotlinx-coroutines needs these at runtime) and small layouts and drawables. The table is now built from the RFC 1951 ranges.
- **`build_apk` produced manifests and layouts Android rejects or ignores.** All found by installing decode→build round trips of four real apps on an Android 7 emulator and diffing the original against the rebuilt binary XML attribute by attribute:
  - Only 26 `android:` attributes had resource IDs, so the rest (`authorities`, `layout_width`, …) were silently ignored (`<provider> does not include authorities attribute`). The full framework table (1580 attrs, with each attr's declared format and every enum/flag value) is now generated from the SDK by `scripts/gen_axml_fw_attrs.py`. `app:` attributes resolve against the app's own `resources.arsc`, and pool entries are keyed on (name, ID) so `android:title` and `app:title` stay distinct.
  - Enum/flag names (`singleTask`, `keyboardHidden|orientation`) were written back as strings (`For input string: "singleTask"`). The decoder and encoder now both use the SDK table, so the round trip is exact. App-defined enums use the new `arsc_attr_enum_value()`.
  - The decoder printed every `res-auto` attribute as `android:` (prefix matched on `strstr(uri, "android")`). It now uses the prefixes the file itself declares.
  - The XML name tokenizer stopped at `.`, so `<androidx.….ConstraintLayout>` became `<androidx>` (`ClassNotFoundException: android.view.androidx`).
  - Entities (`&amp;`, `&apos;`, `&#…;`) were never decoded.
  - Colors were printed as `0x…` hex, floats as `1` (re-read as int), and fractions weren't parsed at all. Typed values long enough to hit a 64-byte buffer were truncated. Dimension/fraction radix used `1 << (radix*2)` instead of Android's 2^0/2^-7/2^-15/2^-23 scaling, so `0.5dp` decoded as `16.0dp`. Complex values now use aapt2's own `floatToComplex()` and print the shortest text that re-encodes to identical bits.
  - String-typed framework attributes are kept as strings (`versionName="1.0"` no longer becomes a float).
  - The decoder's text `res/values*` files and `original/` are no longer packed into the APK.
  - Result: 0 of 2437 binary XML files differ semantically across the four apps (271 of 738 did in one app before).
- **The smali assembler wrote DEX files ART refuses to open:**
  - `annotations_directory_item` lists were written static-then-instance and direct-then-virtual, not sorted by index (`Out-of-order method_idx for annotation`).
  - `string_ids` were sorted bytewise on UTF-8 instead of by UTF-16 code unit (`Out-of-order string_ids`). This differs for characters above U+FFFF and for embedded NULs.
  - Any string that merely looked like a type (`L…;` or `[…`), such as generic-signature fragments (`L:Ljava/lang/Object;`) or regexes, was promoted to a `type_id` (`Invalid type descriptor`). Only well-formed descriptors are promoted now.
- Verified on an Android 7 emulator: decode→build round trips of four real apps now install and run exactly like their re-signed originals (screenshot-checked). Before these fixes, none of them installed. AntiSplit-X can't be round-tripped by any smali rebuilder: it's protected by the ijiami packer, which restores method bodies at runtime against the original DEX layout.

### Known limitation
- A method whose debug info has parameter-name slots that are **all unnamed** (`parameters_size > 0` with every entry `NO_INDEX`) reassembles with `parameters_size = 0` instead: the disassembled text has no `.param` line to carry that count back (baksmali's own output format has the same gap), so a handful of methods lose one blank line in the disassembly on round-trip. No name, register number, line number, or any other data is affected — confirmed cosmetic-only on every real APK tested.

### Changed
- **The project is renamed from `Agent-X` to `Agent-EJ`.** The repository name and URL become `https://github.com/GrandpaEJ/Agent-EJ` (GitHub redirects the old URL), the built binary becomes `agent-ej` — with `agent-ej-nano`, `agent-ej-pico` and the `agent-ej.pid` / `agent-ej.log` runtime files — and every occurrence in the documentation, source comments and test-script defaults follows (`Agent X` in running prose becomes `Agent EJ`). No code behaviour changes: the full suite still reports 159 assertions across 7 tests and 39/39 smali fixtures. `.gitignore` keeps the legacy `agent-x` patterns alongside the new ones so a stale build of the old name can never be committed by accident.

---

## [0.9.0] - 2026-10-27

### Added
- **The decoded `res/values` tree is compiled back into `resources.arsc`** — an edit made after `decode_apk` now reaches the rebuilt APK. `decode_apk` turned every resource value into a `res/values` XML file, but `build_apk` packed whatever `resources.arsc` was on disk, so those files were documentation only: change a string, rebuild, and the APK still had the old value. The new `arsc_compile_xml()` walks the values directories, patches the matching entries of the original table and re-encodes it, and `build_apk` runs it ahead of the pack step (`apk_recompile_resources()`); a table it cannot compile is left in place rather than failing the build.
- **`arsc_compile` tool** — `arsc=<table> res_dir=<res> out=<table>` compiles a decoded tree without a build, reporting how many values changed and how many were left alone.

### Fixed
- **Resource values are never silently dropped or corrupted while recompiling.** Entries are patched in place and an entry whose value already equals what the text says is not touched at all, so decoding and recompiling an untouched tree reproduces the original table **byte for byte** — verified on 16 real APKs. Configuration resolution shares `apk_build_config_str()` with the decoder, so a value in `values-land` reaches the land entry and leaves the default one alone; the entry's own data type decides how its text is read, so a number-shaped string such as `gcm_defaultSenderId` stays a string instead of becoming an integer, colours keep their own type, and values with no text form (dimensions, fractions, dynamic references) are skipped rather than rewritten. Duplicate keys are matched in entry order — one test APK gives every entry of a type the same name, and mapping them all onto the first would have overwritten six of them.

---

## [0.8.4] - 2026-10-27

### Fixed
- **The v1 (JAR) signature now uses the digest the platform's own signer would choose** (#30): the v1 path always signed with SHA-1, which modern JDKs disable — `jarsigner -verify` reported `The jar will be treated as unsigned` plus a `nonexistent signed entries` warning for every APK. The digest now follows apksigner's rule, probed against build-tools 35.0.0: `minSdkVersion >= 18` signs with SHA-256 (jarsigner verifies it with zero warnings), below that it stays SHA-1, because SHA-256 fails `apksigner verify` on API Level 15-17 while SHA-1 verifies at every level. The attribute names mirror apksigner's own (`SHA-256-Digest`, `SHA-256-Digest-Manifest`; jarsigner's `SHA-1-Digest` spelling is rejected by apksigner), and the PKCS#7 block follows the signature file: sha256(SF) signed with SHA256withRSA and digestAlgorithm OID 2.16.840.1.101.3.4.2.1 in both the SignedData set and the SignerInfo. An APK without an AndroidManifest.xml (a JAR-like input) gets SHA-256; an unreadable manifest or a missing uses-sdk falls back to SHA-1, which installs on every Android release. The digest choice matches apksigner's own on every probed fixture (minSdk 15, 17, 18, 24, none).
- **META-INF content files survive v1 signing** (#30): the signer skipped every `META-INF/` entry, so the content files of a real APK (androidx `.version` files, `services/`, build metadata — 71 files in the test APK) were silently dropped from the signed output and never digest-listed. Both reference signers keep and sign them. The skip list now mirrors JDK `SignatureFileVerifier.isSigningRelated()`: only the signature files themselves (MANIFEST.MF, the .SF/.DSA/.RSA/.EC blocks and SIG-* legacy blocks directly under META-INF/) are replaced, in the manifest and in the copy loop alike.
- **Long entry names no longer corrupt or invalidate the v1 manifest** (#30): the JAR spec caps manifest lines at 72 content bytes, but `Name:` lines were written unwrapped — a 613-byte line made jarsigner throw `manifest line too long` and apksigner report `No JAR signatures`, so the output was unverifiable by both reference tools. Name lines now wrap onto leading-space continuation lines exactly the way jarsigner writes them, and both verifiers hash the raw stored section bytes (wrapping included), which is what the section digest covers. The fixed `256 + n*256` manifest buffer also trusted snprintf's return value, so `*out_len` could point past the allocation and the SF hash read heap garbage (measured: 820 claimed for a 768-byte buffer); the SF's `name_line[512]` silently dropped the name of any entry whose Name line reached 512 bytes. Both builders now append into the in-tree growable buffer, and the SF copies the manifest's name lines byte-for-byte, so wrapped names stay wrapped and logical names match on both sides.
- **The PKCS#7 signature is sized by the signing key** (#30): the v1 path embedded a fixed 256-byte `encryptedDigest`, but `rsa_sign` only fills `key->bits/8` bytes — every non-2048-bit key produced a broken CERT.RSA, rejected by apksigner with `Bad signature length: got 256 but was expecting 128` for a 1024-bit key. The digest is now written at exactly the modulus length, the same fix #13 applied to v2/v3.

### Added
- **`tests/test_apk_v1_signing.sh`** (#30): 28 assertions over the v1 signing path with apksigner and jarsigner as oracles — digest selection at the minSdk 17/18 boundary, META-INF preservation, combined v1+v2+v3, a 542-byte wrapped entry name and a 1024-bit key.

---

## [0.8.3] - 2026-10-26

### Fixed
- **An annotation behind a field was written to the wrong place** (#28): a `.annotation` block that follows a `.field` directive was bound to the field, but the block belongs to the field only when the `.end field` terminator closes it — the reference assembler draws the same line. A class with a field and a class level annotation therefore lost the annotation: it went into the field annotation set instead of the class annotation set. The block is parsed on its own now and handed to the field only when the terminator follows.
- **An enum value pointed at an arbitrary field** (#28): an enum value names a field as `Lclass;->name:type`, and the field pool is keyed by the whole reference. A value without the type was written with the field index 0, and baksmali rejected the DEX with `java.lang.IndexOutOfBoundsException: Invalid field index 0, not in [0, 0)`. Such a value is rejected now with the file and the line, the same way the reference assembler rejects it ("expecting COLON"). The initializer of a static field had the same defect in a different place: the reference was only added to the string pool, so the initializer pointed at the field that carries it instead of at the enum constant — it enters the field pool now.
- **The dispatch token was released twice** (memory safety): the `.field` branch released the token of the class level parser when it read a nested annotation block and the parser released it again when the directive ended. The second release handed the block back to the allocator and the next token was read from the recycled memory, so a class with a field and an annotation failed with a directive that was never in the source (`unknown directive '.end' at class level`). AddressSanitizer confirmed the use after free.

### Changed
- The annotation parsing moved into its own module: `smali_annot.c` reads the values, the blocks and releases the annotations, and the class level, the field and the method parser share one implementation instead of three copies that had drifted apart.

---

## [0.8.2] - 2026-10-26

### Fixed
- **DEX 037+ support, correct smali output paths, and crash/bounds hardening** (#26): `dex_parse()` only accepted the `dex\n035\0` magic, so every APK built by a modern AGP was rejected with `Invalid DEX file`. The version check is now an explicit allowlist matching ART (035, 037, 038, 039, 040). `read_dex` derived the output path with `strrchr(class_line, 'L')`, which took the last `L` anywhere in the line and mangled names — `LiteSdkInfo` became `iteSdkInfo` and any package segment containing `L` lost its directory, silently dropping 17 of 5489 classes; the descriptor is now the last whitespace-delimited token, so the package path is preserved. Malformed pool keys reached the DEX writer unguarded and crashed it with a NULL dereference inside `smali_pool_find()`; the field_ids/method_ids loops, the annotation paths that add enum/field/method values, and the instruction paths now all require the same `class->name:type` shape. `comp_proto_sig()` wrote into a fixed 64-entry stack array without bounding the parameter count, overflowing the stack on a proto with more than 64 parameters. Empty DEX sections carried a non-zero offset in the header and the map list, violating constraint G7 (`Offset(N) should be zero when size is zero for field-ids`). The MUTF-8 size counter and the encoder used separate bounds checks, so a truncated sequence was counted as one UTF-16 unit but written as two bytes, violating G15. An unresolved reference was encoded as field/method index 0, silently pointing the instruction at an unrelated member; it now fails with the method name and line number. The class descriptor was used verbatim to build the output path, so a hostile APK with a descriptor such as `L../../tmp/evil` made `read_dex` write outside the output directory; any `..` path component is now rejected. `write_debug_info()` sized its instruction offset table at a hardcoded 4096 entries, truncating the debug info of larger methods. The parser leaked one allocation per `.field`/`.annotation` directive.
- **Native v3 APK signing: the RSA signature did not verify** (#13): the v3 signature machinery was correct all along, the bug was a certificate/key mismatch. `format_apk_sign_block.c` embedded the static built-in `cert_der`/`pubkey_der` when no custom certificate was supplied but signed with the user's private key, so apksigner derived a different public key from the embedded certificate and the signature never verified. `src/crypto/der.c` now derives the canonical `SubjectPublicKeyInfo` from the key limbs (byte-identical to OpenSSL for 2048 and 1024 bit keys) and parses the X.509 certificate instead of scanning for the rsaEncryption OID — the old scan only matched the `30 82` length form, so 1024 bit keys (`0x81`) failed. `parse_pkcs8_der()` skipped the public exponent `e`, leaving `key->e` at zero. Both signing paths now refuse to sign when the certificate does not match the key (`APK_SIGN_ERR_CERT_MISMATCH`), the tool reports `cert_key_mismatch` and exits 1 instead of exiting 0, the signature length follows `key->bits / 8` instead of a hardcoded 256 bytes, and the default key is resolved through `resolve_default_key_path()` so a zero-config run finds `keys/testkey.pem`.
- **Register range validation per instruction format** (#22): the assembler truncated an out-of-range register instead of rejecting it, and the ad-hoc checks were wrong in both directions — F_22x's 16 bit `vB` was never checked, F_3rc and F_31i were not checked at all, and F_51l was checked against a limit that does not apply to it. `src/android/smali/smali_validate.c` holds the limit of every DEX bytecode format (27 rows) and `smali_encoder.c` validates every instruction against it, so an operand above the limit of its format aborts the assembly with a precise diagnostic instead of producing a broken DEX. The F_35c argument registers are resolved once into a shared array that the validator and the encoder both use.
- **Incomplete resource qualifier parsing** (#24): `format_apk_decode_arsc` only understood a part of the `ResTable_config` structure, so the qualifier directories it produced for a decoded APK were wrong for most of the qualifiers aapt2 writes. The full configuration is parsed now — the locale, the screen size and density, the input flags and the night mode — and the qualifiers are emitted in the order of `ResTable_config::toString()`.
- **Gravity enum strings were not parsed** (#25): `format_axml_encode` only understood the numeric form of `android:gravity`, so an XML file with `android:gravity="center"` failed to encode. The enum names are parsed and mapped to their values now.
- **Smali assembler validation and error reporting** (#8): a broken smali file was silently skipped, so the assembler produced a DEX that had quietly lost the class. Every syntax error is now reported as `file:line: message`, an unknown instruction or directive is an error, and the assembly aborts instead of dropping the file. The method signature was truncated as well: `.method public static a(I, I)V` was read as the first token only (`a(I,`), so the DEX carried `a(I)V` and 69 of the 70 parameters were dropped; the signature is read as a whole now. The type list of a proto and the interface list of a class are reused when they are identical instead of being written again, and the parameter type indices no longer live in a fixed 256 entry array that a proto with more parameters could overrun.
- **Debug information was produced but was not usable** (#7): the map list had no `debug_info_item` entry, the parameter and local names were never added to the string pool, the string and type indices were written as plain uleb128 instead of the uleb128p1 the format defines (so every name was off by one), an instruction without a `.line` produced no address advance, `.epilogue` was parsed and then dropped, and `.local v0, "name": <type>` was parsed as the type `":"`. The debug info is written the way the DEX format defines it now, and two fixtures (with `.line`, `.local`, `.param`, `.prologue` and `.epilogue`) assemble to a DEX that baksmali disassembles back to byte-identical smali.

### Added
- **Multi-DEX assembly for large class trees**: every DEX id table is capped at 65535 entries, so assembling a real APK (5489 classes in the test APK) overflowed the method/field tables and produced no output at all. Classes are now ordered by package and split into runs that fit the limit, each written as its own DEX file (`classes.dex`, `classes2.dex`, ...); `smali_assemble_n()` reports how many files were written. `apk_build()` assembles `smali_classesN` in numeric order rather than `readdir()` order, so `classes.dex`, `classes2.dex` and friends land in the right slots inside the APK.
- **Native `resources.arsc` encoder** (#5): `arsc_build()` only patched the global string pool and copied the rest of the file byte for byte, so no resource could ever be changed. The resource table is decoded into an in-memory model — the global value pool, the packages with their type and key pools, and for every type the type spec flags plus one variant per configuration with its entries and values — and written back out again: `arsc_encode()` emits the string pools, the `ResTable_package` chunk and, for every type, the `ResTable_typeSpec` chunk plus one `ResTable_type` chunk per configuration. The layout follows the AOSP rules, so the encoder gives back the table of an unmodified APK **byte for byte**: verified against 17 real APKs (up to 4.8 MB, 236 type chunks, 16,777 pooled strings) and against a fixture that `tests/arsc_build.py` builds without aapt2. The new `arsc_encode` tool reads the `resources.arsc` of an APK or of a raw table, patches values (`set_string=0x7f0f001e=New label`, `set_int=0x7f010002=4711`) and writes the table back out; `arsc_info` prints the packages, the types and the number of configurations as JSON.

### Changed
- The DEX value formatter moved out of `format_dex_smali.c` into its own `format_dex_value.c`, removing a duplication that had drifted between the annotation writer and the static field initializer writer.

---

## [0.8.1] - 2026-06-24

### Fixed
- **Memory bloat from inline annotation arrays** (#25): `smali_field_def_t`, `smali_method_def_t`, and `smali_class_def_t` each embedded `smali_annotation_t annots[MAX_ANNOTS]` (32 slots × 3 KB = 99 KB per struct), so every class/method/field with 0 annotations still paid the full 99 KB cost. Converted to heap-allocated `smali_annotation_t *annots` with lazy grow (`annot_cap` doubles, starts at 4). Added `smali_ctx_free()` to release all per-class, per-method, per-field, per-instruction heap memory at end of `smali_assemble` and `smali_flow`. **Result: smali_assemble peak RSS on 11330-class corpus dropped from ~7.5 GB to 506 MB (15× reduction) and now uses 3× less RAM than `smali.jar` (Java).**

---

## [0.8.0] - 2026-06-24

### Added
- **`smali_flow` 3-mode upgrade** (#24): Split into focused modules (`smali_flow_block.c` / `smali_flow_edge.c` / `smali_flow_mermaid.c` / `smali_flow_class.c`). New `mode` parameter: `basic` (control flow only — invokes/branches/returns/switches/labels), `advance` (all instructions, default), `full` (all instructions with source line numbers). New `scope` parameter: `method` (per-method), `class` (this class → methods → callees call graph), `file` (full cross-class call graph, left-to-right). Mermaid output now uses 6 color-coded shapes (stadium entry, circle return, hexagon throw, asymmetric switch, diamond cond, rectangle normal), dashed edges for try/catch with exception-type labels, and explicit `false` / `case` / `goto` edge labels.

### Fixed
- **`repack_apk` no longer includes smali source files in ZIP** (#19): `collect_files()` now skips top-level `smali*` directories, matching `util_collect_files()` behavior.
- **`smali_assemble` pool memory leak** (#21): `smali_pool_free()` is now called for all 5 pools after writing DEX, reclaiming ~87 MB of heap at assembly end.
- **`apk_analyze` now supports multidex** (#18): Loops through all `classes*.dex` entries in the ZIP instead of only looking for `classes.dex`.
- **`apk_build` cleans up compiled DEX files** (#20): Tracked DEX paths are removed from the source directory after the ZIP is finalized, preventing stale files on repeated builds.
- **`smali_code` guards abstract/native methods** (#23): `write_code_item()` now returns 0 for abstract/native methods, ensuring they have `code_off=0` in the DEX.

---

## [0.7.0] - 2026-06-23

### Added
- **`smali_flow` tool:** Generates Mermaid flowcharts from smali source code, showing method control flow graphs with conditional branches, switch statements, try/catch handlers, and fallthrough edges. Supports single file or directory input with optional method name filter.

---

## [0.6.4] - 2026-06-23

### Fixed
- **Removed noisy `zip_inflate`/`unsupported method` stderr messages** during APK extraction. Non-critical extraction failures are now silently skipped.

---

## [0.6.3] - 2026-06-23

### Added
- **Config-specific resource XMLs:** `decode_apk` now generates per-qualifier resource directories matching apktool output structure (`values-v21/`, `drawable-xhdpi-v4/`, `values-night-v8/`, `drawable-night-v25/`, etc.).
- **ResTable_config qualifier parsing:** Reads density, uiMode (night), and sdkVersion from ARSC type chunk configs, building correct Android resource directory qualifiers.

### Fixed
- **Static buffer overwrite in `arsc_sp_string`:** Type name (`tn`) was a dangling pointer after subsequent calls; now copied to stack buffer.
- **Resource value XMLs kept as text:** Files in `res/values*` directories are no longer AXML-encoded, preserving human-readable resource definitions.
- **Style/complex entry rendering:** Proper `ResTable_map` parsing (12-byte entries with typed `Res_value`), resolved item attribute/value references, parent style references.
- **Color type range:** Extended to handle ARGB8/RGB8 types (0x1C-0x1F) in addition to ARGB4 (0x13-0x16).

---

## [0.6.2] - 2026-06-23

### Fixed
- **AXML enum resolution:** Added `launchMode` (standard/singleTop/singleTask/singleInstance), `screenOrientation` (portrait/landscape/sensor/user/etc.), `configChanges` (proper bitmask decomposition).
- **Gravity/layout_gravity decomposition:** Replaced broken bitwise matching with correct nibble-pair encoding (top|center_horizontal etc.). No more false positives from overlapping bit positions.
- **AXML namespace placement:** `xmlns:` declarations now emitted after regular attributes on a separate indented line, matching apktool output style.
- **AXML indent:** Changed from 2-space to 4-space indent to match apktool.
- **Static buffer reuse:** Fixed `fmt_flags()` buffer not being cleared between calls, causing compounded flag strings across multiple attribute values.

---

## [0.6.1] - 2026-06-23

### Fixed
- **AXML encoder reverse lookup:** Added `arsc_reverse_lookup()` + `axml_assemble_set_arsc()` to resolve `@type/key` references back to binary `TYPE_REFERENCE` during re-encoding, enabling lossless round-trip (decode → encode → build).

---

## [0.6.0] - 2026-06-23

### Added
- **ARSC-Aware AXML Decoder:** Full resource ID resolution (`@ref/0x7f090004` → `@string/show_window`) by parsing the complete `resources.arsc` chunk tree (packages, types, entries).
- **Resource XML Generation:** `decode_apk` now generates `res/values/strings.xml`, `public.xml`, `colors.xml`, `ids.xml`, `bools.xml`, `styles.xml`, and per-type resource files matching apktool's output structure.
- **Android Enum/Flag Resolution:** Hardcoded lookup table for common attributes (orientation, gravity, visibility, scrollbars, layout_gravity) producing human-readable enum names instead of raw hex integers.
- **Extended `arsc_parse()`:** Full package/type/entry resolution API via `arsc_lookup_id()` and `arsc_get_type_name()`, with `axml_set_arsc()` to attach resource context to the AXML decoder.

### Changed
- **`apk_decode()` pipeline:** Now decodes all AXML files (AndroidManifest.xml, res/layout/*.xml) using the `resources.arsc` context for resolved references.
- **AXML type formatting:** Added TYPE_DIMENSION (0x05) and TYPE_FRACTION (0x06) decoding with human-readable unit suffixes (dp, sp, px, pt, in, mm, %, %p).
- **AXML enum values:** `match_parent`/`wrap_content`/`fill_parent` layout constants now decoded from raw INT_DEC values.
- **Color types:** INT_COLOR_ARGB8/ARGB4/RGB8/RGB4 (types 0x13-0x16) now output as hex `#aarrggbb`.

### Fixed
- **String pool static buffer corruption:** `arsc_sp_string()` static `buf` was overwritten by nested calls; type names are now `strdup`'d during parsing.
- **Shift overflow crash:** `arsc_r32()` cast to `uint32_t` before left shifting to prevent undefined behavior on bytes with high-bit set.

---

## [0.5.0] - 2026-06-13

### Added
- **Native APK Signer:** Full implementation of APK Signature Scheme v1, v2, and v3 including chunked hashing, native SHA-256 and RSA-SHA256, PKCS#7/PKCS#8 DER parsing, and dynamic custom certificates.
- **AXML Encoder:** Complete native AXML encoder with 100% AAPT compliance, supporting Resource Maps, namespace URIs, and attribute sorting.
- **Smali Assembler:** Implemented Phase 2 Annotation System (Class, Field, Method, Parameter) and added rigorous validation & error reporting (register limits, type list deduplication).
- **ZipAlign & APK Pipeline:** Lossless native `zipalign` integration and full APK decode/build orchestration logic.
- **Docs & Licenses:** Added `CONTRIBUTING.md` and GNU General Public License v3.0 (`LICENSE`), alongside comprehensive internals documentation for AXML, ARSC, APK Signer, and DEX/Smali.
- **Project Tracking:** Created GitHub issues tracking the remaining native implementation phases (APK Signer, AXML/ARSC Encoders, ZipAlign, Debug Info).

### Changed
- **Architectural Restructure:** Moved source files into a strict semantic layout (`src/core`, `src/net`, `src/tools`) adhering to `AGENTS.md` and the Small File Rule.
- **Android Separation:** Extracted all Android-specific modules (`adb`, `apk`, `axml`, `dex`, `smali`) from `src/formats/` to a dedicated `src/android/` directory.
- **Documentation:** Renamed `AGENT.md` to `AGENTS.md` and moved `REVERSE_ENGINEERING.md` to `docs/RE_ARCHITECTURE.md`.
- **Build System:** Updated `Makefile` to reflect the new directory structure, maintaining full compatibility with `pico` and `nano` targets.
- **Security:** Enabled GitHub branch protection on `main` to prevent force pushes and deletions.

### Fixed
- **Smali:** Corrected MUTF-8 surrogate pair encoding for 4-byte UTF-8 chars.
- **DEX:** Fixed DEX verifier crash and string pool bloat issues.
- **AXML:** Fixed boolean encoding, integer types, and sorted attributes by resource ID.
- **Crypto:** Corrected CERT.RSA signature offset injection length in V1 PKCS#7 template.

---

## [0.4.0] - 2026-06-13

### Added
- Initial Release of the Smali to DEX assembler (`smali_assemble` tool) with full round-trip parity.
- 3-pass contiguous annotation writing (`annotation_item`, `annotation_set_item`, `annotations_directory_item`) ensuring perfect Android Dalvik/ART parser correctness.
- Support for complex static field initializer types and annotation parsing.
- Hash table-based pool lookups for string, type, method, and field pools.

### Fixed
- Fixed memory corruption bugs related to uninitialized `smali_field_def_t`.
- Resolved `List too large for field_annotations list` Android runtime verification crashes in generated DEX files.
- Fixed `encoded_value` size encoding for string, type, and enum references.
- Fixed string pool inflation, null terminators, and string pool bloat issues.
- Fixed duplicate opcodes (e.g., `0x90` collision) and unified instruction parsing logic.

---

## [0.3.1] - 2026-06-01

### Changed
- Upgraded the 15 microstep todo tasks inside the `todo/` directory to align with memory mapping (`mmap`), zero-copy parsing, and strict `< 250 LOC` file limits.
- Injected specific specifications from `apktool_implementation_plan.md` (zipalign padding math, AXML length prefixes, ULEB128 decoding, parameter register mapping, and APK Signing Block formats) into the microstep files.

---

## [0.3.0] - 2026-06-01

### Added
- Integrated reverse engineering dynamic skills in `skills/re/` directory (`re_extract_apk`, `re_search_strings`, `re_analyze_elf`, `re_sign_apk`, and `re_zipalign`).
- Added recursive scanning and lookup in `src/tools.c` to support hierarchical grouping of dynamic agent skills inside subdirectories (e.g. `skills/re/`).

### Changed
- Refactored `execute_dynamic_skill` and `tools_get_definitions` in `src/tools.c` to resolve subfolders safely and prevent directory traversal.

---

## [0.2.1] - 2026-06-01

### Changed
- Added strict memory-first coding guidelines and semantic version tracking rule definitions to [AGENTS.md](AGENTS.md).

---

## [0.2.0] - 2026-06-01


### Added
- Created comprehensive architecture and engineering plan for a custom, zero-dependency APKTool clone (`apktool_implementation_plan.md`), detailing binary AXML, resources.arsc, DEX disassembly/assembly, and v2 APK signatures.

### Changed
- Refactored `AGENT.md` architectural manifest to enforce strict guidelines:
  - **Memory Limits**: Mandatory memory mapping (`mmap`), zero-copy parsing, streaming iterators, and thread-local Arena Allocators to keep RAM footprint < 500 KB.
  - **Code Quality**: Strict file limit of max 250 Lines of Code (LOC) and feature-wise directory layout (`core/`, `net/`, `tools/`, `formats/`, `crypto/`).
  - **Size Optimization**: Compiler configurations targeting < 100 KB binaries via aggressive `-Oz`, Link-Time Optimization (LTO), and section garbage collection.

---

## [0.1.0] - 2026-05-15

### Added
- Integrated system, network, git, text, and Android/Termux skills (69 scripts/schemas) into the dynamic skill engine.

### Removed
- Cleaned up redundant duplicate/generated math and service checking tools to minimize build footprint.

---

## [0.0.0] - 2026-05-01

### Added
- Initial release of Agent EJ (compiled rewrite in pure C11).
- Free AI Backend Integration (no API key required).
- Production Pro features: File-based keyword memory database (RAG), session isolation, dynamic context/token optimization, POSIX daemonization, and structured JSON logging.
