# Agent EJ — Reverse Engineering Architecture

## Table of Contents

1. [Overview](#overview)
2. [APKTool Deep Dive](#apktool-deep-dive)
3. [JADX Deep Dive](#jadx-deep-dive)
4. [Binary Format Reference](#binary-format-reference)
5. [Native C Implementation Plan](#native-c-implementation-plan)
6. [Module Breakdown](#module-breakdown)
7. [Signing & Alignment](#signing--alignment)
8. [Beyond APK — Full RE Suite](#beyond-apk--full-re-suite)
9. [Integration with Agent EJ](#integration-with-agent-ej)
10. [Appendix: Smali Reference](#appendix-smali-reference)

---

## Overview

Agent EJ will become a **native, zero-dependency reverse engineering suite** written in C11. No Java. No Python. No external tools. Everything compiles into a single static binary (~500 KB target).

### Philosophy

| Principle | Why |
|---|---|
| **No exec/shell out** | Every RE operation runs in-process. No `system("apktool")`. |
| **Read-only by default** | Analysis tools never modify the input. Write/repack are explicit. |
| **Streaming where possible** | Parse GB-sized files without loading into RAM. |
| **Format-first design** | Each binary format gets its own `src/format_*.c` module. |

---

## APKTool Deep Dive

APKTool (by Connor Tumbleson, iBotPeaches) does **four things**:

### 1. APK Unpack (`d` command)

```
Input:  app.apk
Output: app/  (decoded directory tree)

app/
├── AndroidManifest.xml    (binary AXML → decoded XML)
├── apktool.yml            (metadata: APK version, build info)
├── assets/                (raw assets, untouched)
├── kotlin/                (Kotlin metadata)
├── lib/                   (native .so files, extracted)
│   ├── armeabi-v7a/
│   ├── arm64-v8a/
│   └── x86_64/
├── original/              (original APK entries, unmodified)
│   ├── AndroidManifest.xml (binary AXML)
│   └── META-INF/          (original signatures — stripped on rebuild)
├── res/                   (decoded resources)
│   ├── drawable/          (.xml → .xml, .9.png → .9.png)
│   ├── layout/            (binary AXML → decoded XML)
│   ├── values/            (resources.arsc → strings.xml, etc.)
│   └── ...
├── smali/                 (DEX → .smali files)
│   ├── com/
│   │   └── example/
│   │       └── MainActivity.smali
│   └── ...
└── unknown/               (unrecognized file types)
```

### 2. Resource Decoding

This is the most complex part. Three binary formats must be parsed:

#### a) AXML (Android Binary XML)

Binary XML used for `AndroidManifest.xml`, layouts, and drawables.

```c
struct axml_header {
    uint8_t  magic[4];      // 0x00080003
    uint32_t file_size;
    uint32_t chunk_count;
};

struct axml_chunk {
    uint16_t type;          // 0x0101 = string pool, 0x0103 = xml start tag
    uint16_t header_size;
    uint32_t chunk_size;
};
```

Key chunk types:
| Type | Name | Content |
|---|---|---|
| `0x0001` | `AXML_CHUNK_STRING_POOL` | All XML string constants |
| `0x0003` | `AXML_CHUNK_XML_START_NAMESPACE` | Namespace declaration |
| `0x0004` | `AXML_CHUNK_XML_END_NAMESPACE` | End namespace |
| `0x0102` | `AXML_CHUNK_XML_START_TAG` | Opening tag with attributes |
| `0x0103` | `AXML_CHUNK_XML_END_TAG` | Closing tag |
| `0x0104` | `AXML_CHUNK_XML_TEXT` | Text node |

Attribute values in AXML are **resource references** (e.g., `@android:string/ok` = `0x01040001`). They must be resolved through `resources.arsc`.

#### b) resources.arsc (Binary Resource Table)

The most complex Android binary format. It's a **chunked** binary tree:

```
resources.arsc
├── RES_TABLE_TYPE (0x0002)
│   ├── RES_STRING_POOL (0x0001) — global string pool
│   └── RES_TABLE_PACKAGE (0x0200) [per package]
│       ├── RES_STRING_POOL — package strings
│       ├── RES_TABLE_TYPE_SPEC (0x0202) [per type]
│       │   └── flags for each entry (1 bit per config)
│       └── RES_TABLE_TYPE (0x0201) [per type + config]
│           ├── RES_TABLE_ENTRY (0x0008) [per resource]
│           │   ├── simple value (uint32)
│           │   └── complex value (map)
│           └── RES_TABLE_ENTRY_FLAGS
```

**Why it's hard:**
- Resources are identified by **32-bit IDs** `0xPPTTEEEE` (Package:Type:Entry)
- Values are **type-encoded** (string ref, boolean, dimension, color, etc.)
- Configurations span 20+ dimensions (locale, screen density, SDK, etc.)
- String pool encoding is **UTF-16** with index-based references

#### c) 9-Patch Images (`.9.png`)

Extended PNG with extra chunks:
- `npCh` — 9-patch metadata (stretch regions, padding)
- `npTc` — 9-patch "optical bounds" (Android 4.3+)

The black border pixels encode stretchable areas and content padding.

### 3. DEX → Smali (`d` command)

This is the **core** of APKTool and the hardest part.

#### DEX File Layout

```c
struct dex_header {
    uint8_t  magic[8];          // "dex\n035\0"
    uint32_t checksum;          // adler32 of everything after checksum
    uint8_t  signature[20];     // SHA-1 of everything after signature
    uint32_t file_size;
    uint32_t header_size;       // 0x70
    uint32_t endian_tag;        // 0x12345678
    uint32_t link_size;
    uint32_t link_offset;
    uint32_t map_offset;        // offset to map_list
    uint32_t string_ids_size;
    uint32_t string_ids_offset;
    uint32_t type_ids_size;
    uint32_t type_ids_offset;
    uint32_t proto_ids_size;
    uint32_t proto_ids_offset;
    uint32_t field_ids_size;
    uint32_t field_ids_offset;
    uint32_t method_ids_size;
    uint32_t method_ids_offset;
    uint32_t class_defs_size;
    uint32_t class_defs_offset;
    uint32_t data_size;
    uint32_t data_offset;
};
```

**ID Sections** (in order):
| Section | Size | Contents |
|---|---|---|
| `string_ids` | 4 bytes each | Offset to MUTF-8 string data |
| `type_ids` | 4 bytes each | Index into `string_ids` (type descriptor) |
| `proto_ids` | 12 bytes each | Shorty, return type, parameter list |
| `field_ids` | 8 bytes each | Class, type, name indices |
| `method_ids` | 8 bytes each | Class, proto, name indices |
| `class_defs` | 32 bytes each | Class access flags, super, interfaces, annotations, data offset |

#### Code Items

Each method's bytecode lives in a `code_item`:

```c
struct dex_code_item {
    uint16_t registers_size;
    uint16_t ins_size;          // incoming arguments
    uint16_t outs_size;         // outgoing arguments
    uint16_t tries_size;
    uint32_t debug_info_offset;
    uint32_t insns_size;        // in 16-bit units
    uint16_t insns[];           // actual bytecode
    // followed by try_items[] + encoded_catch_handler_list
};
```

#### Dalvik Opcode Categories (~256 opcodes total)

| Category | Count | Examples |
|---|---|---|
| `nop` | 1 | `nop` (0x00) |
| `move` | 28 | `move vA, vB`, `move-object`, `move-result` |
| `return` | 6 | `return-void`, `return vA`, `return-object` |
| `const` | 20 | `const/4`, `const/16`, `const-string`, `const-class` |
| `monitor` | 2 | `monitor-enter`, `monitor-exit` |
| `check-cast` | 1 | `check-cast type_id` |
| `instance-of` | 1 | `instance-of vA, vB, type_id` |
| `array` | 6 | `new-array`, `aget`, `aput`, `array-length` |
| `instance` | 3 | `iget`, `iput`, `iput-wide` |
| `static` | 3 | `sget`, `sput`, `sput-wide` |
| `invoke` | 14 | `invoke-virtual`, `invoke-direct`, `invoke-static`, `invoke-interface` |
| `jump` | 16 | `if-eq`, `if-ne`, `if-gt`, `goto`, `packed-switch` |
| `cmp` | 6 | `cmpl-float`, `cmpg-double`, `cmp-long` |
| `unop` | 24 | `neg-int`, `not-long`, `int-to-long` |
| `binop` | 80+ | `add-int`, `sub-long`, `mul-float`, `and-int`, `shr-int` |
| `filled-array` | 6 | `filled-new-array`, `fill-array-data` |
| `throw` | 1 | `throw vA` |
| `invoke-polymorphic` | 4 | Method handle support (API 26+) |

#### Smali Format

Smali is a **human-readable text representation** of DEX bytecode, one file per class:

```smali
.class public Lcom/example/MainActivity;
.super Landroid/app/Activity;
.source "MainActivity.java"

# annotations
.annotation system Ldalvik/annotation/MemberClasses;
    value = {
        Lcom/example/MainActivity$MyAdapter;
    }
.end annotation

# direct methods
.method public constructor <init>()V
    .registers 1
    invoke-direct {p0}, Landroid/app/Activity;-><init>()V
    return-void
.end method

# virtual methods
.method public onCreate(Landroid/os/Bundle;)V
    .registers 3
    .param p1, "savedInstanceState"    # Landroid/os/Bundle;

    .prologue
    .line 12
    invoke-super {p0, p1}, Landroid/app/Activity;->onCreate(Landroid/os/Bundle;)V
    .line 14
    const v0, 0x7f030001
    invoke-virtual {p0, v0}, Lcom/example/MainActivity;->setContentView(I)V
    .line 16
    return-void
.end method
```

### 4. APK Repack (`b` command)

```
Input:  app/  (decoded directory)
Output: app_repack.apk

Steps:
1. Smali → DEX     (assemble all .smali → classes.dex, classes2.dex, ...)
2. Resources rebuild (decoded XML → AXML, values/ → resources.arsc)
3. ZIP archive       (store, no compression for .dex and .arsc)
4. zipalign          (4-byte alignment for memory-mapped entries)
5. Sign              (APK Signature Scheme v1 + v2)
```

---

## JADX Deep Dive

JADX (by Skylot) decompiles DEX → Java source code. It has a fundamentally different architecture from APKTool:

### Pipeline

```
APK/ZIP
  │
  ▼
DEX Input ──► DEX Reader ──► IR (Intermediate Representation) ──► Java AST ──► Java Output
  │               │                      │                           │
  .dex            │                      │                           │
  .odex           ├─ String pool         ├─ Basic blocks             ├─ Type inference
  .jar            ├─ Type hierarchy      ├─ Control flow graph       ├─ Generics recovery
  .class          ├─ Method bytecodes    ├─ Data flow analysis       ├─ Deobfuscation
                  ├─ Exception table     ├─ Inlining                 └─ Syntax output
                  └─ Debug info          └─ SSA form
```

### Key Components

#### a) DEX Reader (`dex-reader` module)
- Loads DEX into memory
- Resolves all ID tables (strings, types, methods, fields, protos)
- Parses code items, debug info, annotations
- Provides lazy access (not everything is loaded at once)

#### b) IR Converter (`dex-ir` module)
- Converts Dalvik bytecodes → **SSA (Static Single Assignment)** IR
- Each Dalvik instruction maps to one or more IR instructions
- Basic blocks are identified (straight-line code sequences)
- Control flow graph (CFG) is built from branch targets and exception handlers

#### c) Decompiler (`dex-decompiler` module)
JADX's decompiler is **type-inference based** (not pattern-based like older tools):

1. **Type Inference**: Determines Java types from Dalvik types including:
   - Generic type signatures (`List<String>` → erased in bytecode)
   - Varargs reconstruction
   - Enum reconstruction
   - Anonymous class detection

2. **Control Flow Recovery**:
   - Structured exception handling (try/catch/finally)
   - Switch statement recovery (packed/sparse switches)
   - Loop detection (for, while, do-while)
   - Ternary operator recovery

3. **Deobfuscation**:
   - Incremental renaming (`a`, `b`, `c` → meaningful names based on usage)
   - Inline single-use methods
   - Constant propagation
   - Dead code elimination

#### d) Java Output (`java-output` module)
- AST → formatted Java source
- Annotation generation
- Import management
- Javadoc from debug info

### Why JADX is Hard to Reimplement

| Feature | Complexity | Why |
|---|---|---|
| Type inference | Extreme | Must recover erased generics, reconstruct interfaces |
| Exception handling | Very High | `try/catch` in bytecode is a flat table, needs structural recovery |
| CFG reconstruction | High | Must handle irreducible CFGs, dead code |
| Data flow analysis | Very High | Defining in SSA, constant propagation, copy propagation |
| Deobfuscation | High | Needs heuristics, context analysis |

---

## Binary Format Reference

### ZIP (APK Container)

APK is a standard ZIP with specific constraints:

```
[local file header 1]
  [file data 1]
[local file header 2]
  [file data 2]
  ...
[central directory header 1]
  ...
[central directory header n]
[end of central directory record]
```

**APK-specific rules:**
- `classes.dex` must be stored (uncompressed)
- `resources.arsc` must be stored
- All entries must be 4-byte aligned (offset % 4 == 0)
- `META-INF/MANIFEST.MF`, `META-INF/*.SF`, `META-INF/*.RSA` are mandatory for signed APKs

### DEX Opcode Encoding

Dalvik uses a variable-length instruction format:

| Format | Size | Structure | Examples |
|---|---|---|---|
| `10x` | 2 bytes | `op` | `nop` |
| `12x` | 2 bytes | `op vA, vB` | `move v1, v2` |
| `11n` | 2 bytes | `op vA, #+B` | `const/4 v0, 1` |
| `21c` | 4 bytes | `op vAA, type@BBBB` | `const-class v0, Ljava/lang/String;` |
| `22c` | 4 bytes | `op vA, vB, type@CCCC` | `instance-of v0, v1, Ljava/io/Serializable;` |
| `35c` | 6 bytes | `op {vC, vD, vE, vF, vG}, method@BBBB` | `invoke-virtual` |
| `3rc` | 6 bytes | `op {vCCCC..vCCCC+AAAA-1}, method@BBBB` | `invoke-static/range` |
| `31t` | 6 bytes | `op vAA, offset` | `packed-switch`, `fill-array-data` |

### ELF (Executable and Linkable Format)

For native `.so` libraries in APK `lib/`:

```c
struct elf_header {
    uint8_t  ident[16];     // \x7fELF + class + data + version + OS/ABI
    uint16_t type;           // ET_DYN (shared object)
    uint16_t machine;        // EM_ARM, EM_AARCH64, EM_386, EM_X86_64
    uint32_t version;
    uint64_t entry;
    uint64_t phoff;          // program header offset
    uint64_t shoff;          // section header offset
    uint32_t flags;
    uint16_t ehsize;
    uint16_t phentsize;
    uint16_t phnum;
    uint16_t shentsize;
    uint16_t shnum;
    uint16_t shstrndx;
};
```

For RE we care about:
- **Dynamic symbols** (`.dynsym` + `.dynstr`) — exported/imported functions
- **Sections** — `.text`, `.rodata`, `.data`, `.bss`, `.got`, `.plt`
- **Relocations** — how the loader resolves addresses
- **String table** — embedded strings
- **ARM/Thumb interwork** — detecting ARM vs Thumb mode

---

## Native C Implementation Plan

### Architecture

```
agent-ej binary
│
├── src/
│   ├── main.c              (entry point, CLI dispatch)
│   ├── agent.c             (AI agent loop, tool dispatch)
│   ├── tools.c             (existing tool implementations)
│   ├── http.c              (HTTP client for AI API)
│   ├── logger.c            (structured logging)
│   ├── telegram.c          (Telegram bot polling)
│   │
│   ├── dex.c               DEX reader + disassembler
│   ├── dex_ir.c            DEX → IR conversion (for decompilation)
│   ├── dex_decompile.c     IR → Java AST (decompiler)
│   ├── axml.c              AXML → XML decoder
│   ├── axml_enc.c          XML → AXML encoder (for rebuild)
│   ├── arsc.c              resources.arsc reader
│   ├── arsc_enc.c          resources → arsc encoder
│   ├── zip.c               ZIP reader/writer (wrapper around miniz)
│   ├── ninepatch.c         9-patch decoder
│   ├── smali.c             Smali → DEX assembler
│   ├── sign.c              APK signer (v1 + v2)
│   ├── align.c             zipalign
│   │
│   ├── elf.c               ELF parser
│   ├── macho.c             Mach-O parser (for iOS)
│   ├── pe.c                PE parser (for Windows)
│   │
│   ├── disasm_arm.c        ARM/Thumb disassembler
│   ├── disasm_x86.c        x86/x64 disassembler
│   │
│   └── hash.c              SHA-1, SHA-256, MD5 (minimal implementations)
│
├── include/
│   ├── dex.h
│   ├── axml.h
│   ├── arsc.h
│   ├── zip.h
│   ├── smali.h
│   ├── elf.h
│   ├── disasm.h
│   └── hash.h
│
└── vendor/
    └── miniz/             (single-file ZIP library, Apache 2.0)
```

### Data Flow

```
APK Input
    │
    ├─► zip_open() ─────────────────────► zip.c
    │       │
    │       ├─► "classes.dex" ──────────► dex.c
    │       │       │
    │       │       ├─► dex_parse_header()  → verify, dump string/type/method tables
    │       │       ├─► dex_dump_code()     → opcode disassembly
    │       │       ├─► dex_to_smali()      → full Smali output
    │       │       └─► dex_decompile()     → Java source output
    │       │
    │       ├─► "AndroidManifest.xml" ───► axml.c
    │       │       └─► axml_decode()       → human-readable XML
    │       │
    │       ├─► "resources.arsc" ────────► arsc.c
    │       │       └─► arsc_dump()         → resource table dump
    │       │
    │       └─► "lib/arm64-v8a/*.so" ───► elf.c
    │               └─► elf_dump_symbols()  → exported/imported symbols
    │
    └─► Output: decoded directory, analysis JSON, or human-readable report
```

---

## Module Breakdown

### Phase 1: ZIP + AXML (Weeks 1-2)

#### `src/zip.c` — ZIP Container

```c
// Core API
void*    zip_open(const char* path);                    // returns zip_archive*
void     zip_close(void* archive);
int      zip_get_num_entries(void* archive);
char*    zip_get_entry_name(void* archive, int index);
size_t   zip_get_entry_size(void* archive, int index);
int      zip_entry_is_compressed(void* archive, int index);
void*    zip_extract_entry(void* archive, int index, size_t* out_size);  // returns malloc'd data
int      zip_extract_all(void* archive, const char* out_dir);            // extract to directory
```

Implementation: Either wrap `miniz` (`vendor/miniz/miniz.h`) or implement ZIP parsing from scratch. Miniz is ~7000 lines of C, well-tested, single-file. We should vendor it.

#### `src/axml.c` — AXML Decoder

```c
typedef struct axml_ctx axml_ctx;

axml_ctx* axml_decode(const uint8_t* data, size_t size);
void      axml_free(axml_ctx* ctx);
const char* axml_get_xml(axml_ctx* ctx);        // returns decoded XML string
int         axml_get_tag_count(axml_ctx* ctx);

// Low-level access for code analysis
int         axml_get_attribute_count(axml_ctx* ctx, int tag_index);
const char* axml_get_attribute_name(axml_ctx* ctx, int tag_index, int attr_index);
const char* axml_get_attribute_value(axml_ctx* ctx, int tag_index, int attr_index);
uint32_t    axml_get_attribute_resource_id(axml_ctx* ctx, int tag_index, int attr_index);
```

**AXML parsing algorithm:**
```
1. Read header, validate magic (0x00080003)
2. Walk chunks:
   a. String Pool (type=0x0001):
      - Parse string pool header
      - Build string table (UTF-16 → UTF-8 conversion)
   b. XML chunks (types 0x0102-0x0104):
      - Start tag: read namespace URI, tag name (both string pool indices)
      - For each attribute: read namespace, name, value (string or typed)
      - End tag: match to start tag
3. Emit XML text:
   - Indent properly
   - Resolve resource references (@android:style/TextAppearance)
   - Handle namespaces
```

### Phase 2: DEX Reader (Weeks 3-4)

#### `src/dex.c` — DEX Core

```c
typedef struct dex_ctx dex_ctx;

dex_ctx*  dex_parse(const uint8_t* data, size_t size);
void      dex_free(dex_ctx* ctx);

// Validation
int       dex_verify_magic(dex_ctx* ctx);
int       dex_verify_checksum(dex_ctx* ctx);
int       dex_verify_signature(dex_ctx* ctx);
const char* dex_get_error(dex_ctx* ctx);

// Strings
int       dex_get_string_count(dex_ctx* ctx);
const char* dex_get_string(dex_ctx* ctx, int idx);

// Types
int       dex_get_type_count(dex_ctx* ctx);
const char* dex_get_type(dex_ctx* ctx, int idx);   // e.g., "Ljava/lang/String;"

// Protos
int       dex_get_proto_count(dex_ctx* ctx);
const char* dex_get_proto_shorty(dex_ctx* ctx, int idx);
int       dex_get_proto_return_type(dex_ctx* ctx, int idx);
int       dex_get_proto_param_count(dex_ctx* ctx, int idx);
int*      dex_get_proto_param_types(dex_ctx* ctx, int idx);  // indices into type_ids

// Fields
int       dex_get_field_count(dex_ctx* ctx);
int       dex_get_field_class(dex_ctx* ctx, int idx);
int       dex_get_field_type(dex_ctx* ctx, int idx);
int       dex_get_field_name(dex_ctx* ctx, int idx);

// Methods
int       dex_get_method_count(dex_ctx* ctx);
int       dex_get_method_class(dex_ctx* ctx, int idx);
int       dex_get_method_proto(dex_ctx* ctx, int idx);
int       dex_get_method_name(dex_ctx* ctx, int idx);

// Classes
int       dex_get_class_count(dex_ctx* ctx);
int       dex_get_class_type(dex_ctx* ctx, int idx);
int       dex_get_class_access_flags(dex_ctx* ctx, int idx);
int       dex_get_class_super(dex_ctx* ctx, int idx);
int       dex_get_class_interface_count(dex_ctx* ctx, int idx);
int*      dex_get_class_interfaces(dex_ctx* ctx, int idx);
int       dex_get_class_method_count(dex_ctx* ctx, int idx);
int       dex_get_class_methods(dex_ctx* ctx, int idx);  // indices into method_ids

// Code (bytecode access)
dex_code* dex_get_method_code(dex_ctx* ctx, int method_idx);
int       dex_code_get_registers(dex_code* code);
int       dex_code_get_ins(dex_code* code);
int       dex_code_get_outs(dex_code* code);
int       dex_code_get_insn_count(dex_code* code);
uint16_t* dex_code_get_insns(dex_code* code);           // raw bytecodes

// Try/catch
int       dex_code_get_try_count(dex_code* code);
dex_try*  dex_code_get_tries(dex_code* code);           // start_addr, end_addr, catch_addr, catch_type
```

### Phase 3: DEX Disassembler (Weeks 4-6)

#### `src/dex.c` — Disassembly

```c
// Single instruction decode
typedef struct {
    uint16_t    opcode;         // Dalvik opcode value
    const char* opcode_name;    // e.g., "move", "invoke-virtual"
    int         fmt;            // instruction format (10x, 12x, 35c, etc.)
    int         vA;             // destination register
    int         vB;             // source register / small literal
    int         vC;             // second source register
    int         vD;             // third source register (for 35c)
    int         vE;             // fourth source register
    int         vF;             // fifth source register
    int         vG;             // sixth source register
    uint16_t    reg_count;      // register count for range formats
    uint32_t    raw_B;          // raw reference (method_idx, type_idx, field_idx)
    int32_t     literal;        // signed literal value
    uint32_t    target;         // branch target (byte offset from instruction start)
} dex_insn;

// Disassemble one instruction
// Returns the number of 16-bit code units consumed
int dex_disassemble_insn(const uint16_t* code, uint32_t offset, dex_insn* out);

// Helper: resolve reference names
const char* dex_resolve_method(dex_ctx* ctx, uint32_t method_idx);
const char* dex_resolve_type(dex_ctx* ctx, uint32_t type_idx);
const char* dex_resolve_field(dex_ctx* ctx, uint32_t field_idx);
const char* dex_resolve_string(dex_ctx* ctx, uint32_t string_idx);

// Full disassembly of a method to text (Smali format)
char* dex_disassemble_method(dex_ctx* ctx, int method_idx);
```

**Opcode dispatch table** (~256 entries):

```c
typedef void (*opcode_handler)(const uint16_t* code, uint32_t offset, dex_insn* out);

static const opcode_handler opcode_handlers[256] = {
    [0x00] = handle_nop,
    [0x01] = handle_move,
    [0x02] = handle_move_from16,
    // ... all ~256 opcodes
    [0x6E] = handle_invoke_virtual,
    [0x6F] = handle_invoke_super,
    [0x70] = handle_invoke_direct,
    [0x71] = handle_invoke_static,
    [0x72] = handle_invoke_interface,
    // ...
    [0xE8] = handle_invoke_polymorphic,
    [0xE9] = handle_invoke_custom,
    // ...
};
```

### Phase 4: Smali Assembler (Weeks 6-8)

This is the **most complex** single module. It needs:

```c
typedef struct smali_ctx smali_ctx;

// Parse a Smali file → internal AST
smali_ctx* smali_parse(const char* smali_text, size_t len);

// Assemble → DEX bytecode
uint8_t*  smali_assemble(smali_ctx* ctx, size_t* out_size);

// Error reporting
int       smali_get_error_count(smali_ctx* ctx);
const char* smali_get_error(smali_ctx* ctx, int idx);

void      smali_free(smali_ctx* ctx);
```

**Smali assembler components:**
1. **Lexer**: Tokenize Smali text (directives, opcodes, registers, types, labels)
2. **Parser**: Build a per-class AST:
   - Class header (`.class`, `.super`, `.source`, `.implements`)
   - Annotations (`.annotation`, `.end annotation`)
   - Fields (`.field`)
   - Methods (`.method`, `.end method`)
   - Method body (`.registers`, `.param`, `.prologue`, `.line`, instructions, labels)
3. **Assembler**: Walk AST → DEX structures:
   - Build type pool (deduplicate all referenced types)
   - Build string pool (deduplicate all strings)
   - Build method/field/proto refs
   - Encode instructions with proper register addressing
   - Handle try/catch blocks (try_items + encoded_catch_handler_list)
   - Lay out debug info (`.line`, `.param`, `.prologue`)
4. **Writer**: Emit the complete DEX file with correct headers

### Phase 5: ARSC Reader (Weeks 7-8)

#### `src/arsc.c` — Resource Table

```c
typedef struct arsc_ctx arsc_ctx;

arsc_ctx* arsc_parse(const uint8_t* data, size_t size);
void      arsc_free(arsc_ctx* ctx);

// Package enumeration
int          arsc_get_package_count(arsc_ctx* ctx);
const char*  arsc_get_package_name(arsc_ctx* ctx, int pkg_idx);
uint32_t     arsc_get_package_id(arsc_ctx* ctx, int pkg_idx);  // 0x7f for app

// Type enumeration (per package)
int          arsc_get_type_count(arsc_ctx* ctx, int pkg_idx);
const char*  arsc_get_type_name(arsc_ctx* ctx, int pkg_idx, int type_idx);
// "string", "drawable", "layout", "id", "style", "attr", etc.

// Resource lookup
const char*  arsc_resolve_value(arsc_ctx* ctx, uint32_t res_id);
// 0x7f030001 → "layout/activity_main.xml"
// 0x01040001 → "android:string/ok"
```

### Phase 6: APK Signer (Weeks 8-9)

```c
// APK Signature Scheme v1 (JAR signing)
int sign_jar(const char* apk_path, const char* keystore_path, const char* key_alias, const char* password);

// APK Signature Scheme v2 (block-based, more efficient)
int sign_v2(const char* apk_path, const char* keystore_path, const char* key_alias, const char* password);

// Helper: generate signing key
int sign_generate_key(const char* keystore_path, const char* key_alias, const char* password);
```

### Phase 7: ELF/PE/Mach-O Parsing (Weeks 9-10)

```c
// ELF
elf_ctx* elf_parse(const uint8_t* data, size_t size);
int      elf_get_symbol_count(elf_ctx* ctx);
const char* elf_get_symbol_name(elf_ctx* ctx, int idx);
uint64_t elf_get_symbol_address(elf_ctx* ctx, int idx);
int      elf_section_count(elf_ctx* ctx);
const char* elf_section_name(elf_ctx* ctx, int idx);

// String extraction
char**   elf_extract_strings(elf_ctx* ctx, int min_len, int* count);

// Architecture detection (ARM, Thumb, AArch64, x86, x64)
int      elf_get_arch(elf_ctx* ctx);    // returns enum { ARCH_ARM, ARCH_ARM64, ARCH_X86, ARCH_X64 }
```

---

## Signing & Alignment

### APK Signature Scheme v1 (JAR Signing)

```
META-INF/MANIFEST.MF
  └── SHA1 digest of every file in the APK (base64-encoded)

META-INF/CERT.SF
  └── SHA1 digest of MANIFEST.MF + individual digests

META-INF/CERT.RSA
  └── PKCS7 signature of CERT.SF (RSA + SHA1)
```

Implementation:
```c
// 1. For each file in APK (excluding META-INF/):
//    - Compute SHA-1 digest
//    - Write to MANIFEST.MF as base64

// 2. Compute SHA-1 of MANIFEST.MF
//    - Write to CERT.SF

// 3. Sign CERT.SF with private key → PKCS7 DER
//    - Write to CERT.RSA
```

### APK Signature Scheme v2

```
[APK Contents]
[APK Signing Block]          ← new in v2
  ├── Block ID 0x7109871a   ← v2 signer block
  │   ├── Signed data
  │   │   ├── Digests
  │   │   ├── Certificates
  │   │   └── Additional attributes
  │   └── Signatures
  └── Block ID 0x42726577   ← v3 signer block (optional)
[Central Directory]
[End of Central Directory]
```

### zipalign

```c
// 4-byte alignment for memory-mapped entries
// Ensures that all uncompressed entries start at a 4-byte boundary

int zipalign(const char* in_apk, const char* out_apk, int alignment);
// alignment = 4 for APK
```

**Algorithm:**
```
1. Read input ZIP
2. For each local file header, adjust data_offset to be alignment-aligned
3. Insert padding bytes between header and data as needed
4. Update central directory offsets
5. Rewrite EOCD
```

---

## Beyond APK — Full RE Suite

### Executable Format Support

| Format | File | Use Case | C Module |
|---|---|---|---|
| ELF | `.so`, Linux binaries | Native libs in APK, Linux RE | `elf.c` |
| Mach-O | `.dylib`, `.o`, iOS | iOS app analysis | `macho.c` |
| PE | `.dll`, `.exe` | Windows malware, games | `pe.c` |

### Architecture Disassemblers

| ISA | Module | Initial Support |
|---|---|---|
| ARM (32-bit) | `disasm_arm.c` | Full ARM + Thumb, ~400 instructions |
| AArch64 | `disasm_arm64.c` | Full AArch64, ~600 instructions |
| x86 (32-bit) | `disasm_x86.c` | All legacy instructions |
| x86-64 | `disasm_x64.c` | Full amd64, AVX, SSE |

**Why write our own disassembler instead of using Capstone?**

| Factor | Custom C | Capstone |
|---|---|---|
| Binary size | +50 KB | +500 KB |
| Dependencies | None | None (Capstone is C too) |
| Build complexity | Part of agent-ej | Separate library |
| Updates | We maintain | Active community |
| Control | Full | Full |

**Verdict**: Use Capstone for phase 1 (it's already C, well-maintained, permissive license). If binary size is critical, write a minimal custom disassembler later.

### Decompilation (DEX → Java)

**Phase 1 — Smali disassembly** (APKTool-level):
- DEX bytecode → Smali text
- ~250 opcode handlers
- Try/catch block recovery
- Debug info integration

**Phase 2 — Structured decompilation** (JADX-level):
- Basic block detection
- Control flow graph (CFG)
- Structured exception recovery
- Loop detection
- Variable type inference
- Java source code emission

This is by far the hardest component. A JADX-level decompiler in C would be ~20,000+ LOC and requires advanced compiler techniques (SSA construction, data flow analysis). A pragmatic approach:

1. **Short-term**: Capstone for native code disassembly + our DEX → Smali
2. **Medium-term**: Structured decompilation with goto elimination
3. **Long-term**: Full type-inference-based Java decompiler

---

## Integration with Agent EJ

### New Tools (registered in `tools.c`)

When the above modules are built, we register them as agent-callable tools:

```c
// In tools_get_definitions():
{ "analyze_apk",
  "Decompile and analyze an Android APK file. Extracts and returns manifest, dex info, resources, and native libraries.",
  { "path": {"type": "string", "description": "Path to the APK file"} }
},
{ "dump_dex",
  "Disassemble a DEX file to human-readable Smali output.",
  { "path": {"type": "string", "description": "Path to the DEX file"} },
  { "class": {"type": "string", "description": "Optional: specific class to dump (e.g., Lcom/example/MainActivity;)"} }
},
{ "read_axml",
  "Decode a binary Android XML file to human-readable XML.",
  { "path": {"type": "string", "description": "Path to the binary XML file"} }
},
{ "dump_arsc",
  "Dump the resource table from resources.arsc in a readable format.",
  { "path": {"type": "string", "description": "Path to resources.arsc"} }
},
{ "analyze_elf",
  "Analyze an ELF binary (symbols, strings, sections, architecture).",
  { "path": {"type": "string", "description": "Path to the ELF file"} }
},
{ "disassemble",
  "Disassemble native code (ARM, x86) to assembly text.",
  { "path": {"type": "string", "description": "Path to the binary file"} },
  { "arch": {"type": "string", "description": "Architecture: arm, thumb, arm64, x86, x64"} }
},
{ "extract_apk",
  "Extract an APK to a directory (unzip + decode resources).",
  { "path": {"type": "string", "description": "Path to the APK file"} },
  { "out_dir": {"type": "string", "description": "Output directory"} }
},
{ "rebuild_apk",
  "Rebuild an APK from a decoded directory and sign it.",
  { "dir": {"type": "string", "description": "Decoded directory"} },
  { "output": {"type": "string", "description": "Output APK path"} },
  { "sign": {"type": "boolean", "description": "Whether to sign the APK"} }
},
{ "sign_apk",
  "Sign an unsigned APK with a generated or provided keystore.",
  { "path": {"type": "string", "description": "Path to the unsigned APK"} }
},
{ "search_strings",
  "Extract and search strings from any binary file (APK, DEX, ELF, SO).",
  { "path": {"type": "string", "description": "Path to the binary file"} },
  { "pattern": {"type": "string", "description": "Optional regex to filter strings"} },
  { "min_len": {"type": "number", "description": "Minimum string length (default: 4)"} }
}
```

### Agent Workflow

The agent (powered by the AI model) will chain these tools together for complex RE tasks:

```
User: "Find the API endpoint in this APK"

Agent:
1. analyze_apk("target.apk")
   → Gets package name, activities, permissions
2. dump_dex("target.apk/classes.dex", class="Lcom/example/MainActivity;")
   → Gets disassembly of main activity
3. search_strings("target.apk", pattern="https?://")
   → Finds URL strings
4. analyze_elf("target.apk/lib/arm64-v8a/libnative.so")
   → Checks if native libs have interesting symbols
5. Formats results as a cohesive report
```

---

## Appendix: Smali Reference

### Directives

| Directive | Purpose |
|---|---|
| `.class <access> <name>` | Class declaration |
| `.super <type>` | Superclass |
| `.source "<file>"` | Source file name |
| `.implements <type>` | Interface implementation |
| `.annotation <visibility> <name>` | Annotation block start |
| `.end annotation` | Annotation block end |
| `.field <access> <name>:<type>` | Field declaration |
| `.method <access> <name>(<params>)<return>` | Method declaration |
| `.end method` | Method end |
| `.registers N` | Register count |
| `.locals N` | Non-parameter local register count |
| `.param <name>` | Parameter name (from debug info) |
| `.prologue` | Code prologue marker |
| `.line N` | Source line number |
| `.catch <type> {<from> .. <to>} <handler>` | Exception handler |
| `.catchall {<from> .. <to>} <handler>` | Catch-all handler |
| `.packed-switch <key>, :<table>` | Packed switch data |
| `.sparse-switch` | Sparse switch data |
| `.array-data` | Array fill data |
| `.end array-data` | End array data |

### Registers

| Syntax | Meaning |
|---|---|
| `v0` .. `v65535` | Local register |
| `p0` .. `p65535` | Parameter register (p0 = this for non-static) |
| `vA` / `vB` / `vC` | Generic register notation in format descriptions |

### Types

| Smali | Java |
|---|---|
| `V` | `void` |
| `Z` | `boolean` |
| `B` | `byte` |
| `S` | `short` |
| `C` | `char` |
| `I` | `int` |
| `J` | `long` |
| `F` | `float` |
| `D` | `double` |
| `Lpackage/name/ClassName;` | Object type |
| `[I` | `int[]` |
| `[[Ljava/lang/String;` | `String[][]` |

### Common Opcodes Quick Reference

| Opcode | Format | Mnemonic | Description |
|---|---|---|---|
| `0x00` | 10x | `nop` | No operation |
| `0x01` | 12x | `move vA, vB` | Move register |
| `0x04` | 12x | `move-object vA, vB` | Move object reference |
| `0x07` | 12x | `move-wide vA, vB` | Move 64-bit value |
| `0x0e` | 11x | `return-void` | Return from void method |
| `0x10` | 11n | `return vA` | Return 32-bit value |
| `0x12` | 21c | `const-string vAA, string@BBBB` | Load string constant |
| `0x1c` | 21c | `const-class vAA, type@BBBB` | Load class reference |
| `0x22` | 22c | `new-instance vA, type@BBBB` | Create new object |
| `0x6e` | 35c | `invoke-virtual {vC..}, method@BBBB` | Virtual method call |
| `0x70` | 35c | `invoke-direct {vC..}, method@BBBB` | Direct method call |
| `0x71` | 35c | `invoke-static {vC..}, method@BBBB` | Static method call |
| `0x74` | 3rc | `invoke-virtual/range {vC..}, method@BBBB` | Range virtual call |
| `0x32` | 22c | `iget vA, vB, field@CCCC` | Instance field get |
| `0x34` | 22c | `iput vA, vB, field@CCCC` | Instance field put |
| `0x60` | 22c | `sget vA, field@BBBB` | Static field get |
| `0x62` | 22c | `sput vA, field@BBBB` | Static field put |
| `0x3c` | 23x | `cmpg-double vAA, vBB, vCC` | Compare doubles |
| `0x32` | 12x | `if-eq vA, vB, :label` | Branch if equal |
| `0x38` | 21t | `if-eqz vAA, :label` | Branch if zero |
| `0x28` | 10t | `goto :label` | Unconditional branch |

---

*This document is a living reference. As agent-ej's RE capabilities grow, each module here becomes a real `.c` file in `src/`.*
