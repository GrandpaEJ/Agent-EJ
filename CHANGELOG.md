# Changelog

All notable changes to the Agent X project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

**Versioning Rule:**
- `n.x.x` : [Lts] (Long Term Support) releases
- `x.n.x` : [new feat]
- `x.x.n` : [fixed]

---

## [Unreleased]

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
- Added strict memory-first coding guidelines and semantic version tracking rule definitions to [AGENT.md](file:///home/grandpa/me/code/zig/agent-x/AGENT.md).

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
- Initial release of Agent X (compiled rewrite in pure C11).
- Free AI Backend Integration (no API key required).
- Production Pro features: File-based keyword memory database (RAG), session isolation, dynamic context/token optimization, POSIX daemonization, and structured JSON logging.
