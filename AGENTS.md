# Agent EJ — Architecture & Refactoring Manifest

Agent EJ is designed to be an ultra-lightweight, zero-dependency, and extremely fast AI autonomous agent. To scale its features (such as custom binary parsing and APKTool capabilities) without bloat, it must adhere to a strict modular design. 

This manifest outlines the refactoring guidelines to split the code into small, feature-wise, reusable, size-optimized, and low-RAM-consuming components.

---

## 1. Feature-Wise File System Layout

The codebase must be reorganized into modular feature directories under `src/` and `include/`. No monolithic files are allowed.

```
agent-ej/
├── include/                 # Header declarations
│   ├── core.h               # Core agent loop, structures, and config
│   ├── net.h                # HTTP client and Telegram bot
│   ├── tools.h              # Tool dispatch and registration definitions
│   ├── formats.h            # ZIP, AXML, ARSC, DEX, and ELF parser APIs
│   └── crypto.h             # Hashes (SHA1, SHA256) and signatures
│
├── src/
│   ├── core/                # Core agent orchestrator
│   │   ├── main.c           # CLI parsing and daemon startup
│   │   ├── agent.c          # AI agent execution loop
│   │   └── logger.c         # Structured JSON logging
│   │
│   ├── net/                 # Networking interfaces
│   │   ├── http.c           # Free Pollinations API / HTTP integration
│   │   └── telegram.c       # Long-polling Telegram daemon
│   │
│   ├── tools/               # Agent tools (POSIX wrapper scripts & internal functions)
│   │   ├── tool_dispatch.c  # Main tool registration and parsing
│   │   ├── tool_fs.c        # File read, write, delete, search functions
│   │   ├── tool_sys.c       # Subprocess running and terminal commands
│   │   └── tool_net.c       # Curl downloads and external integration
│   │
│   ├── formats/             # General binary format parsing/writing
│   │   ├── format_zip.c     # ZIP operations (wrapping miniz)
│   │   └── format_elf.c     # ELF parser (native .so symbol dumper)
│   │
│   ├── android/             # Android-specific operations and parsers
│   │   ├── adb/             # Android Debug Bridge client protocol
│   │   ├── apk/             # Full APK rebuild pipeline
│   │   ├── axml/            # Android Binary XML parser and encoder
│   │   ├── dex/             # DEX binary parser and baksmali decompiler
│   │   └── smali/           # Smali assembler (DEX compilation)
│   │
│   └── crypto/              # Minimal cryptographic implementations
│       ├── hash.c           # MD5, SHA-1, SHA-256
│       └── sign.c           # JAR (v1) and APK (v2) signature block generators
```

---

## 2. The Small File Rule (Max 250 LOC)

To prevent code complexity and ensure clean reviews, files must remain small and tightly focused.
- **Rule**: No source file (`.c`) should exceed **250 lines of code** (excluding header files and comments).
- **Single Responsibility Principle**: A file must do one thing. For example, do not mix ZIP directory alignment with file extraction. Keep them in separate translation units.
- **Header Isolation**: Public functions must be documented and declared in `include/`, while internal helper functions must be marked `static` and kept in the `.c` file.

---

## 3. Designing for Reusability

All format decoders and helper utilities must be **decoupled** from the agent's main state.
- **No Global State**: Variables like current directory, agent config, or JSON state must not be globally accessed. Always pass context pointers (e.g. `axml_ctx*` or `dex_ctx*`).
- **Encapsulated Memory**: Every module must follow a consistent Lifecycle Pattern:
  ```c
  // Initialization & Parsing
  module_ctx* module_parse(const uint8_t* data, size_t size);
  
  // Logical operations (read-only)
  const char* module_get_property(module_ctx* ctx, int index);
  
  // Serialization (rebuild)
  uint8_t*    module_serialize(module_ctx* ctx, size_t* out_size);
  
  // Cleanup
  void        module_free(module_ctx* ctx);
  ```

---

## 4. RAM Footprint Optimization (Magically Low RSS)

To keep RAM usage under **500 KB** (even when processing large APKs/DEX files), we employ the following memory management techniques:

### 4.1. Memory Mapping (`mmap`)
Do not load target binary files (like APKs or `.dex` files) into heap memory using `malloc` + `fread`. 
* Use `mmap()` (POSIX) to map files directly into the virtual address space in read-only mode.
* The OS handles page demand caching, keeping the Resident Set Size (RSS) close to zero.

### 4.2. Zero-Copy Parsing
Instead of creating heap-allocated objects for metadata, parse structures **in-place** directly from the `mmap` pointer.
* Use direct byte-offset casting to structures.
* Store pointers referencing string locations inside the mapped file rather than allocating new strings.

### 4.3. Streaming and Iterators
For heavy tables (like the DEX String Table or resources.arsc Key table), do not build loaded lookup tables.
* Implement **iterators** that scan the tables sequentially or perform binary searches on sorted index arrays inside the mapped byte buffer.

### 4.4. Arena Allocators
For tools that require temporary allocations (e.g., JSON processing, Smali generation):
* Implement a simple thread-local Arena Allocator.
* Allocate a single small block of memory (e.g., 64 KB) at startup.
* Sub-allocate inside the arena with simple pointer increments.
* Free the entire block at once at the end of the action, completely eliminating heap fragmentation and GC overhead.

---

## 5. Binary Size Optimization (Low Executable Target)

The static binary must compile to under **100 KB** for CLI/Pico mode.

### 5.1. Compiler Optimization Flags
Use the following compiler flags inside the `Makefile` or `build.zig` to minimize executable size:
```make
# Size-optimized compilation
CFLAGS += -Oz                      # Optimize for size aggressively (closer to machine instructions)
CFLAGS += -ffunction-sections      # Split functions into their own sections
CFLAGS += -fdata-sections          # Split global variables into their own sections
CFLAGS += -flto                    # Enable Link Time Optimization (cross-module tree shaking)

# Linker dead-code stripping
LDFLAGS += -Wl,--gc-sections       # Garbage collect unused sections (strips dead code)
LDFLAGS += -Wl,--strip-all         # Strip all debugging symbols and symbol tables
```

### 5.2. Avoiding Runtime Bloat
- **Standard Library Formatting**: Avoid heavy `printf`/`sprintf` calls with complex format strings when writing small utilities. Utilize simple string concatenations or custom write-based micro-formatters.
- **Dynamic Allocations**: Avoid linking against bloated custom malloc implementations. Stick to the basic POSIX system allocator or our custom Arena.

---

## 6. Development Workflow, Memory Focus & Versioning

To ensure codebase integrity, resource efficiency, and trackable milestones:

### 6.1. Memory-First Focus
Every single code change, refactoring step, or feature implementation **must focus on memory usage** as the highest priority:
* Run memory leak checks (e.g. via Valgrind or static analysis tools) with every build.
* Always favor zero-copy pointers, stack buffers, and arena-managed blocks over standard heap allocation.

### 6.2. Git & Semantic Versioning Rules
* **Track with Git**: All changes must be cleanly staged and committed to git.
* **Changelog Updates**: Every release and major update must be documented in [CHANGELOG.md](file:///home/grandpa/me/code/zig/agent-ej/CHANGELOG.md) following Keep a Changelog standards.
* **Semantic Versioning**: Adhere strictly to the following versioning format:
  * **1.x.x** : [Lts] (Long Term Support / Major Architectural Updates)
  * **x.n.x** : [new feat] (New features and backward-compatible additions)
  * **x.x.n** : [fixed] (Bug fixes and patches)
* **Release Tags**: Every commit that updates the version inside the changelog must be tagged in git (e.g. `git tag v0.4.0`).

---

## 7. DEX⇄Smali Round-Trip Parity

### 7a. DEX→Smali (Baksmali) — COMPLETE (113/113)

The native DEX→Smali disassembler is split into focused files under `src/android/dex/`:
- `format_dex_smali_util.c` — shared utilities (aflags, sb, res, mproto, reg_name, uleb, string escaping)
- `format_dex_smali_annot.c` — annotation parsing with recursive encoded_value, blank line between items
- `format_dex_smali_method.c` — method disassembly with try/catch, switch payloads, access$ comments, float hints
- `format_dex_smali.c` — class-level orchestrator with static field initializers (encoded_value sign-ext, type 0x17 strings)

**113/113 classes byte-identical** with `baksmali.jar` output.

### 7b. Smali→DEX (Assembler) — Implementation Plan

The assembler exists at `src/android/smali/` (~2,850 LOC) with a working pipeline but critical gaps. The goal is a perfect assembler matching `smali.jar` output — disassemble→assemble should produce byte-identical DEX.

#### Phase 1: Bug Fixes & Critical Gaps (Foundation)

| # | Task | Files | Priority | Notes |
|---|------|-------|----------|-------|
| 1.1 | Fix opcode 0x90 duplicate | `smali_insn_parser.c` | Critical | `0x90` listed as both `add-int` and `int-to-char`, linear lookup returns first match making `int-to-char` unreachable |
| 1.2 | Fix const-string-jumbo kind | `smali_insn_parser.c` | Critical | kind=25 should be kind=1 (string reference) |
| 1.3 | Enable debug info writing | `smali_writer.c` | High | `write_debug_info()` exists but `debug_info_off` hardcoded to 0 (line 456). Wire it up and emit `.line`/`.locals`/`.prologue`/`.epilogue` |
| 1.4 | MUTF-8 string encoding | `smali_writer.c` | Critical | DEX requires Modified UTF-8 (`\0` → `0xC0 0x80`, supplementary chars as surrogate pairs). Currently writes raw C strings |
| 1.5 | Static field initializer types | `smali_parser.c`, `smali_writer.c` | High | Only `int` initial values supported. Need: `null`, `boolean`, `byte`, `short`, `char`, `long`, `float`, `double`, `string`, `type`, `.enum`, `array` encoded values |
| 1.6 | Annotation writing | `smali_parser.c`, `smali_writer.c` | High | `annotations_off` always 0. Need: `annotation_directory_item`, `annotation_set_item`, `annotation_item`, `annotation_set_ref_list` for class/field/method/param annotations |

#### Phase 2: Annotation System (Full Correctness)

| # | Task | Files | Notes |
|---|------|-------|-------|
| 2.1 | Parse `.annotation` blocks in smali_parser | `smali_parser.c` | Currently skipped entirely. Need `smali_annotation_t` struct with visibility, type, elements |
| 2.2 | Parse annotation values (all types) | `smali_parser.c` | `.enum`, `.array`, sub-annotations, string/type/field/method refs |
| 2.3 | Write `annotation_directory_item` | `smali_writer.c` | Per-class annotation directory with offsets to field/method/param/class annotation sets |
| 2.4 | Write `annotation_set_item` + `annotation_item` | `smali_writer.c` | Encoded_annotation with ULEB128 type_idx + name/value pairs |
| 2.5 | Write `annotation_set_ref_list` | `smali_writer.c` | For method parameter annotations |
| 2.6 | Handle `system` vs `runtime` vs `build` visibility | `smali_parser.c` | Map to visibility byte (0=build, 1=runtime, 2=system) |

#### Phase 3: Debug Info (Line Numbers + Locals)

| # | Task | Files | Notes |
|---|------|-------|-------|
| 3.1 | Wire up `write_debug_info()` | `smali_writer.c` | Un-comment the call, set `debug_info_off` in code_item |
| 3.2 | Emit line number state machine | `smali_writer.c` | `DBG_ADVANCE_LINE`, `DBG_ADVANCE_PC`, `DBG_START_LOCAL`, `DBG_END_LOCAL`, `DBG_END_SEQUENCE` |
| 3.3 | Encode `.local` entries | `smali_writer.c` | `DBG_START_LOCAL` (name+type) and `DBG_START_LOCAL_EXTENDED` (name+type+signature) |
| 3.4 | Encode `.param` entries | `smali_writer.c` | Parameter name strings |
| 3.5 | Handle `.prologue`/`.epilogue` | `smali_writer.c` | `DBG_SET_PROLOGUE_END`, `DBG_SET_EPILOGUE_BEGIN` markers |

#### Phase 4: DEX Validation & Correctness

| # | Task | Files | Notes |
|---|------|-------|-------|
| 4.1 | Register range validation | `smali_encoder.c` | Warn/error on vA>255 (F_21c), vB>15 (F_12x), vA>65535 (F_22x), etc. |
| 4.2 | Abstract/native methods → code_off=0 | `smali_writer.c` | Skip code_item for abstract/native methods, set `code_off = 0` in class_data |
| 4.3 | String pool sort validation | `smali_pool.c` | Verify strict lexicographic ordering, reject duplicate strings |
| 4.4 | Out/ins register computation | `smali_writer.c` | Verify `outs_size` from invoke instructions, compute `ins_size` from param count |
| 4.5 | `align_4` between code items | `smali_writer.c` | Already done (commit a49aced). Verify padding is zero-filled |
| 4.6 | SHA-1 and Adler-32 signing | `smali_writer.c` | Already exists in `smali_hash.c`. Verify header fields at offsets 0-31 |
| 4.7 | `map_list` generation | `smali_writer.c` | Already written. Verify all section types are included and sorted by offset |
| 4.8 | Byte-identical test harness | Test script | Assemble smali→DEX, then disassemble back. Both directions should be stable |

#### Phase 5: Pool & Performance

| # | Task | Files | Notes |
|---|------|-------|-------|
| 5.1 | Hash table for pool lookups | `smali_pool.c` | Replace O(n²) linear scans with hash table (string pool can be 10K+ entries) |
| 5.2 | Type list deduplication | `smali_pool.c` | Deduplicate identical parameter type lists across proto IDs |
| 5.3 | Memory cleanup | `smali.c` | Call `smali_pool_free()` and free class/method/field strings |
| 5.4 | Error reporting | `smali_parser.c` | Add line numbers, syntax error messages, recovery |

#### Phase 6: APK Rebuild Integration

| # | Task | Files | Notes |
|---|------|-------|-------|
| 6.1 | AXML encoder | `format_axml.c` | Binary XML writer for AndroidManifest.xml |
| 6.2 | resources.arsc encoder | `format_arsc.c` | Binary resource table writer |
| 6.3 | APK rebuild pipeline | `format_apk.c` | smali→DEX + manifest + resources → ZIP → align → sign |
| 6.4 | APK v1 signing | `crypto/sign.c` | JAR signing with SHA-1/SHA-256 digests |
| 6.5 | zipalign | `format_zip_write.c` | 4-byte alignment for uncompressed entries |

### Implementation Order

**Phase 1** first (bugs + critical gaps), then **Phase 2** (annotations — blocks round-trip), then **Phase 3** (debug info), then **Phase 4** (validation), **Phase 5** (performance), **Phase 6** (APK rebuild).

### Testing Strategy

- Test DEX: `apk/classes.dex` (113 classes from TopActivity app)
- Reference: `java -jar apk/baksmali.jar d classes.dex -o /tmp/ref`
- Assemble: `./agent-ej tool smali_assemble src_dir=/tmp/ref out_dex=/tmp/out.dex`
- Verify: `java -jar apk/smali.jar a /tmp/ref -o /tmp/smali_out.dex && diff classes.dex /tmp/smali_out.dex`
- Round-trip: disassemble→assemble→disassemble should be stable

### Current Assembler File Map

```
src/android/smali/
├── smali.c              (34 LOC)  — Entry point: smali_assemble()
├── smali_parser.c       (307 LOC) — .class/.field/.method parsing, instruction dispatch
├── smali_lexer.c        (88 LOC)  — Tokenization, string literals, register parsing
├── smali_insn_parser.c  (688 LOC) — Opcode table, instruction format parsing
├── smali_encoder.c      (230 LOC) — Bytecode encoding (raw insn→uint16_t words)
├── smali_pool.c          (437 LOC) — String/type/proto/field/method pool building+soring
├── smali_writer.c        (1019 LOC) — DEX binary serialization (full layout)
├── smali_hash.c          (49 LOC)  — Adler-32 + SHA-1
└── smali_internal.h      (11 LOC)  — Internal declarations
```

