# Baksmali/Smali — Complete Feature Reference

A comprehensive catalog of every feature, utility, and pattern in the baksmali/smali toolchain. Intended as a parity checklist for the native C11 agent-ej reimplementation.

---

## 1. Toolsuite Overview

| Tool | Purpose | Input | Output |
|---|---|---|---|
| **baksmali** | DEX disassembler | `.dex`, `.apk`, `.oat`, `.odex` | `.smali` directory |
| **smali** | DEX assembler | `.smali` files/dirs | `.dex` file |
| **dexlib2** | DEX I/O library (Java) | DEX bytes/API | DEX model objects |
| **smalidea** | IntelliJ IDEA plugin | `.smali` files | IDE (debugging, navigation) |
| **deodexerant** | On-device helper binary | Device framework | Inline/vtable dumps |

---

## 2. Baksmali — Commands & Options

### 2.1 `disassemble` (d)

```
baksmali disassemble [options] <file>
```

**Core options:**
| Flag | Description |
|---|---|
| `-o,--output <dir>` | Output directory (default: `out/`) |
| `-a,--api <level>` | Target API level (default: auto-detect) |
| `--classes <list>` | Comma-separated class filter |
| `-j,--jobs <n>` | Thread count (default: CPU cores) |
| `-l,--use-locals` | Output `.locals N` instead of `.registers N` |
| `--accessor-comments` | Comments for synthetic accessor methods |
| `--allow-odex-opcodes` | Preserve odex-specific opcodes |
| `--code-offsets` | Comment each instruction with its byte offset |
| `--debug-info` | Output `.local`, `.param`, `.line` etc. (default: true) |
| `--implicit-references` | Omit class name for same-class field/method refs |
| `--normalize-virtual-methods` | Use base-class method references |
| `--parameter-registers` | Use `pNN` syntax for parameter registers (default: true) |
| `--sequential-labels` | Sequential label numbering instead of address-based |
| `--resolve-resources <prefix> <public.xml>` | Annotate resource IDs with names from public.xml |
| `--register-info <spec>` | Add register-type comments around instructions |

**Register info specifiers:** `ALL`, `ALLPRE`, `ALLPOST`, `ARGS`, `DEST`, `MERGE`, `FULLMERGE`

**Bootclasspath:**
| Flag | Description |
|---|---|
| `-b,--bootclasspath <path>` | Boot classpath files (colon-separated) |
| `-c,--classpath <path>` | Additional classpath files |
| `-d,--classpath-dir <dir>` | Directory containing classpath files (repeatable) |

**Input format:** Supports multi-dex APKs via `app.apk/classes2.dex` syntax.

### 2.2 `dump` (du)

```
baksmali dump [options] <file>
```

Annotated hex dump of a DEX file showing header, sections, and map list.

### 2.3 `list` (l)

```
baksmali list <subcommand> <file>
```

| Subcommand | Lists |
|---|---|
| `classes` (c) | All class names in the DEX |
| `methods` (m) | All methods in the method table |
| `fields` (f) | All fields in the field table |
| `strings` (s) | All strings in the string table |
| `types` (t) | All type descriptors |
| `dex` (d) | DEX entries in an APK/oat |
| `dependencies` (dep) | Dependencies of an odex/oat |
| `fieldoffsets` (fo) | Instance field offsets per class |
| `vtables` (v) | Virtual method tables per class |

### 2.4 `deodex` (de)

```
baksmali deodex [options] <file>
```

Deodexes `.odex`/`.oat` files back to DEX. Requires framework bootclasspath. All `disassemble` options apply plus `--inline-table` for custom inline method tables.

---

## 3. Smali — Commands & Options

### 3.1 `assemble` (a)

```
smali assemble [options] <file|dir>+
```

| Flag | Description |
|---|---|
| `-o,--output <file>` | Output DEX path (default: `out.dex`) |
| `-a,--api <level>` | Target API level (default: 15) |
| `-j,--jobs <n>` | Thread count |
| `--allow-odex-opcodes` | Accept odex-specific opcodes |
| `--verbose` | Verbose error messages |

Accepts `.smali` files or directories (recursively scanned).

---

## 4. Smali Language Features

### 4.1 Class Structure
```
.class <access-flags> <descriptor>
.super <descriptor>
.source "<filename>"
.implements <descriptor>
```

### 4.2 Access Flags
`public` `private` `protected` `static` `final` `synchronized` `bridge` `varargs` `native` `interface` `abstract` `strictfp` `synthetic` `annotation` `enum` `constructor`

### 4.3 Fields
```
.field <access-flags> <name>:<type> [= <value>]
```

Static field initializers use encoded values (integers, strings, enums, arrays, annotations).

### 4.4 Methods
```
.method <access-flags> <name><prototype>
    .registers N          ; total registers
    .locals N             ; local (non-parameter) registers
    .param pN, "<name>"   ; debug parameter name
    .line N               ; debug line number
    .local vN, "<name>"   ; debug local variable
    .prologue
    .epilogue
    <instructions>
.end method
```

### 4.5 Annotations
```
.annotation <visibility> <type>
    <name> = <value>
.end annotation
```

Visibilities: `build` (0), `runtime` (1), `system` (2)

### 4.6 Array Data
```
.array-data N
    <value1>
    <value2>
    ...
.end array-data
```

### 4.7 Try/Catch Blocks
```
:try_start_0
    <instructions>
:try_end_0
.catch <exception-type> {:try_start_0 .. :try_end_0} :handler_label
.catchall {:try_start_0 .. :try_end_0} :handler_label
```

### 4.8 Labels
```
:label_name
:cond_N          ; conditional branch target
:goto_N          ; unconditional branch target
:try_start_N     ; try block start
:try_end_N       ; try block end
:switch_N        ; switch table
:pswitch_N       ; packed-switch target
:sswitch_N       ; sparse-switch target
```

### 4.9 Packed/Sparse Switch
```
:switch_data
    .packed-switch <first-key>
        :pswitch_0
        :pswitch_1
        ...
    .end packed-switch

    .sparse-switch
        <key1> -> :sswitch_0
        <key2> -> :sswitch_1
        ...
    .end sparse-switch
```

---

## 5. Register Convention

| Convention | Syntax | Raw mapping |
|---|---|---|
| v-register | `v0`, `v1`, ... | Direct: `vN` = raw register N |
| p-register | `p0`, `p1`, ... | Offset: `pN` = raw register `(regs - ins) + N` |
| .registers | Total registers (params + locals) | |
| .locals | Local (non-parameter) registers only | |

Parameters are at the END of the register range: `v0..v(locals-1)` = locals, `v(locals)..v(regs-1)` = params (aliased as `p0..p(ins-1)`).

---

## 6. Dalvik Opcodes (Full Set)

### 6.1 Nop & Move (0x00–0x0D)
`nop` `move` `move/from16` `move/16` `move-wide` `move-wide/from16` `move-wide/16` `move-object` `move-object/from16` `move-object/16` `move-result` `move-result-wide` `move-result-object` `move-exception`

### 6.2 Return (0x0E–0x11)
`return-void` `return` `return-wide` `return-object`

### 6.3 Constants (0x12–0x1C)
`const/4` `const/16` `const` `const/high16` `const-wide/16` `const-wide/32` `const-wide` `const-wide/high16` `const-string` `const-string-jumbo` `const-class`

### 6.4 Monitor (0x1D–0x1E)
`monitor-enter` `monitor-exit`

### 6.5 Type Check (0x1F–0x20)
`check-cast` `instance-of`

### 6.6 Array (0x21–0x26)
`array-length` `new-instance` `new-array` `filled-new-array` `filled-new-array/range` `fill-array-data`

### 6.7 Throw & Branch (0x27–0x2C)
`throw` `goto` `goto/16` `goto/32` `packed-switch` `sparse-switch`

### 6.8 Compare (0x2D–0x31)
`cmpl-float` `cmpg-float` `cmpl-double` `cmpg-double` `cmp-long`

### 6.9 Conditional Branch (0x32–0x3D)
`if-eq` `if-ne` `if-lt` `if-ge` `if-gt` `if-le` `if-eqz` `if-nez` `if-ltz` `if-gez` `if-gtz` `if-lez`

### 6.10 Array Get/Put (0x44–0x51)
`aget` `aget-wide` `aget-object` `aget-boolean` `aget-byte` `aget-char` `aget-short` `aput` `aput-wide` `aput-object` `aput-boolean` `aput-byte` `aput-char` `aput-short`

### 6.11 Instance Field (0x52–0x5F)
`iget` `iget-wide` `iget-object` `iget-boolean` `iget-byte` `iget-char` `iget-short` `iput` `iput-wide` `iput-object` `iput-boolean` `iput-byte` `iput-char` `iput-short`

### 6.12 Static Field (0x60–0x6D)
`sget` `sget-wide` `sget-object` `sget-boolean` `sget-byte` `sget-char` `sget-short` `sput` `sput-wide` `sput-object` `sput-boolean` `sput-byte` `sput-char` `sput-short`

### 6.13 Invoke (0x6E–0x78)
`invoke-virtual` `invoke-super` `invoke-direct` `invoke-static` `invoke-interface` `invoke-virtual/range` `invoke-super/range` `invoke-direct/range` `invoke-static/range` `invoke-interface/range`

### 6.14 Unary Math (0x7B–0x8F)
`neg-int` `not-int` `neg-long` `not-long` `neg-float` `not-float` `neg-double` `not-double` `int-to-long` `int-to-float` `int-to-double` `long-to-int` `long-to-float` `long-to-double` `float-to-int` `float-to-long` `float-to-double` `double-to-int` `double-to-long` `double-to-float` `int-to-byte` `int-to-char` `int-to-short`

### 6.15 Binary Math (0x90–0xCF)
`add-int` `sub-int` `mul-int` `div-int` `rem-int` `and-int` `or-int` `xor-int` `shl-int` `shr-int` `ushr-int` then same for long, float, double. Plus `/2addr` variants (0xB2–0xCF).

### 6.16 Literal Math (0xD0–0xE2)
`add-int/lit16` `rsub-int/lit16` `mul-int/lit16` `div-int/lit16` `rem-int/lit16` `and-int/lit16` `or-int/lit16` `xor-int/lit16` `add-int/lit8` `rsub-int/lit8` `mul-int/lit8` `div-int/lit8` `rem-int/lit8` `and-int/lit8` `or-int/lit8` `xor-int/lit8` `shl-int/lit8` `shr-int/lit8` `ushr-int/lit8`

### 6.17 Extended (DEX 037+)
`invoke-polymorphic` (45cc) `invoke-custom` (fc) `const-method-handle` (fe) `const-method-type` (ff)

---

## 7. Instruction Formats (Code Units)

| Format | ID | Words | Example |
|---|---|---|---|
| 10x | φ | 1 | `return-void` |
| 12x | B\|A\|op | 1 | `move vA, vB` |
| 11n | B\|A\|op | 1 | `const/4 vA, #+B` |
| 11x | AA\|op | 1 | `return vAA` |
| 10t | AA\|op | 1 | `goto +AA` |
| 20t | φ\|op AAAA | 2 | `goto/16 +AAAA` |
| 30t | φ\|op AAAA AAAA | 3 | `goto/32 +AAAAAAAA` |
| 21c | AA\|op BBBB | 2 | `const-string vAA, string@BBBB` |
| 22c | B\|A\|op CCCC | 2 | `iget vA, vB, field@CCCC` |
| 21t | AA\|op +BBBB | 2 | `if-eqz vAA, +BBBB` |
| 22t | B\|A\|op +CCCC | 2 | `if-eq vA, vB, +CCCC` |
| 21s | AA\|op #+BBBB | 2 | `const/16 vAA, #+BBBB` |
| 22b | AA\|op BB #+CC | 2 | `add-int/lit8 vAA, vBB, #+CC` |
| 23x | AA\|op BB CC | 2 | `add-int vAA, vBB, vCC` |
| 35c | G\|A\|op BBBB F\|E\|D\|C | 3 | `invoke-virtual {vC..vG}, meth@BBBB` |
| 3rc | AA\|op BBBB CCCC | 3 | `invoke-virtual/range {vCCCC..vNNNN}, meth@BBBB` |
| 31i | AA\|op #+BBBBBBBB | 3 | `const vAA, #+BBBBBBBB` |
| 51l | AA\|op BBBBBBBBBBBBBBBB | 5 | `const-wide vAA, #+BBBBBBBBBBBBBBBB` |
| 31t | AA\|op +BBBBBBBB | 3 | `fill-array-data vAA, +BBBBBBBB` |
| 21h | AA\|op #+BBBB0000 | 2 | `const/high16 vAA, #+BBBB0000` |
| 22s | B\|A\|op #+CCCC | 2 | `add-int/lit16 vA, vB, #+CCCC` |
| 22x | AA\|op BBBB | 2 | `move/from16 vAA, vBBBB` |
| 32x | φ\|op AAAA BBBB | 3 | `move/16 vAAAA, vBBBB` |
| 31c | AA\|op BBBBBBBB | 3 | `const-string-jumbo vAA, string@BBBBBBBB` |

---

## 8. DEX Format Sections

| Section | Map ID | Type |
|---|---|---|
| header_item | 0x0000 | File header (magic, checksum, signature, sizes, offsets) |
| string_id_item | 0x0001 | String table (offset→MUTF-8 data) |
| type_id_item | 0x0002 | Type descriptors (index→string_id) |
| proto_id_item | 0x0003 | Method prototypes (shorty, return, params) |
| field_id_item | 0x0004 | Field references (class, type, name) |
| method_id_item | 0x0005 | Method references (class, proto, name) |
| class_def_item | 0x0006 | Class definitions (type, access, super, interfaces, annotations, class_data, static_values) |
| call_site_id_item | 0x0007 | Call sites (DEX 037+) |
| method_handle_item | 0x0008 | Method handles (DEX 037+) |
| map_list | 0x1000 | Section directory |
| type_list | 0x1001 | Interface/parameter type lists |
| annotation_set_ref_list | 0x1002 | Parameter annotation lists |
| annotation_set_item | 0x1003 | Annotation set (array of annotation_off) |
| class_data_item | 0x2000 | Class field/method data (ULEB128 encoded) |
| code_item | 0x2001 | Method bytecode (registers, ins, outs, tries, insns) |
| string_data_item | 0x2002 | MUTF-8 string data |
| debug_info_item | 0x2003 | Debug info (line numbers, local variables, parameters) |
| annotation_item | 0x2004 | Single annotation (visibility + encoded_annotation) |
| encoded_array_item | 0x2005 | Static field initial values |
| annotations_directory_item | 0x2006 | Class annotation directory |

---

## 9. Encoded Value Types

| Type | Value | Smali representation |
|---|---|---|
| BYTE | 0x00 | `0x%xt` |
| SHORT | 0x02 | `0x%xs` |
| CHAR | 0x03 | `0x%xu` |
| INT | 0x04 | `0x%x` |
| LONG | 0x06 | `0x%xL` |
| FLOAT | 0x10 | `%gf` |
| DOUBLE | 0x11 | `%g` |
| STRING | 0x17 | `"string"` |
| TYPE | 0x18 | `Lcom/example/Type;` |
| FIELD | 0x19 | `.enum Lclass;->field:type` |
| METHOD | 0x1A | `Lclass;->method(...)type` |
| ENUM | 0x1B | `.enum Lclass;->field:type` |
| ARRAY | 0x1C | `{ element, element, ... }` |
| ANNOTATION | 0x1D | `@Lannotation(...)` |
| NULL | 0x1E | `null` |
| BOOLEAN | 0x1F | `true` / `false` |

---

## 10. Debug Info Directives

```
.prologue                    ; method prologue end
.epilogue                    ; method epilogue start
.source "<file>"             ; source file name
.line <N>                    ; line number mapping
.local vN, "<name>", <type> [= <sig>]  ; local variable debug info
.param pN, "<name>" [= <sig>]         ; parameter debug info
.end local vN                ; end local variable scope
.restart local vN            ; restart local variable scope
```

---

## 11. dexlib2 — Library API (Java)

The Java library underlying both tools. Provides:

- **DexFile** — Parse/write DEX files
- **ClassDef** — Class definitions with methods, fields, annotations
- **Method** / **MethodImplementation** — Method bytecode with try/catch, debug info
- **Field** — Field definitions with initial values
- **Instruction** — All Dalvik instruction types
- **DexRewriter** — Rewrite/modify DEX files programmatically
- **MultiDexContainer** — Multi-dex APK support
- **DexBackedDexFile** — Zero-copy mmap-backed DEX access
- **DexFormatter** — Format DEX objects to smali text
- **DexFormattedWriter** — Output writer for smali generation

---

## 12. smalidea — IDE Plugin

IntelliJ IDEA / Android Studio plugin for smali:
- Syntax highlighting
- Code navigation (go to definition, find usages)
- Register value watching
- Breakpoint debugging (via JDWP)
- Method/field rename refactoring
- Error checking and warnings

---

## 13. Baksmali-Specific Algorithms

### 13.1 Register Renumbering
Baksmali analyzes register liveness and data flow, then renumbers registers so that:
- Each register has a single consistent purpose through its lifetime
- The resulting smali is readable (e.g., counter in `v0`, result in `v1`)
- The data flow is semantically correct

This is the feature that fixes verification issues when raw DEX registers have broken data flow (e.g., raw reg 0 used for multiple unrelated purposes).

### 13.2 Virtual Method Normalization
`--normalize-virtual-methods` resolves virtual method calls to the base class where the method was originally declared, rather than the static type used in the invoke instruction.

### 13.3 Implicit References
`--implicit-references` omits the class name for field/method references within the current class (e.g., `iget v0, p0, field` instead of `iget v0, p0, Lthis/Class;->field:type`).

### 13.4 Resource Resolution
`--resolve-resources` maps integer constants in bytecode to Android resource names by cross-referencing a `public.xml` file.

### 13.5 Accessor Comments
`--accessor-comments` adds helper comments to synthetic accessor methods generated by the compiler (e.g., `# access$000(this)`).

### 13.6 Code Offset Comments
`--code-offsets` adds a comment before each instruction with its byte offset in the method (e.g., `# 0x0057`).

### 13.7 Register Info
`--register-info` adds comments showing register types before/after each instruction, using the classpath to resolve types. Options: `ALL`, `ALLPRE`, `ALLPOST`, `ARGS`, `DEST`, `MERGE`, `FULLMERGE`.

---

## 14. Smali Assembler Algorithms

### 14.1 String/Type/Proto/Field/Method Pool Deduplication
All identifiers in the smali source are collected into pools. The assembler deduplicates, sorts, and assigns indices for the DEX ID tables.

### 14.2 Prototype Deduplication & Sorting
Method prototypes (`(Args)Return`) are deduplicated and sorted by return type, then by parameter list. Shorty strings are auto-generated.

### 14.3 Method/Field Index Assignment
Methods and fields are sorted by class, then name, then type/prototype. Delta encoding is used in class_data.

### 14.4 Try/Catch Block Encoding
Try ranges and catch handler types are collected per method and encoded as `tries` in the `code_item` with LEB128-encoded handler lists.

### 14.5 Debug Info Encoding
Line numbers, local variables, and parameter names from `.line`, `.local`, `.param` directives are encoded as `debug_info_item` in the DEX.

### 14.6 Checksum & Signature
Adler32 checksum and SHA-1 signature are computed over the final DEX file.

---

## 15. Agent-EJ Native Parity Status

### Completed
- [x] DEX disassembly to smali (113 classes, 28/113 byte-identical to baksmali)
- [x] Smali assembly to DEX (pool building, header, map list, code items)
- [x] All 242 Dalvik opcodes (decoder + encoder)
- [x] Class-level annotations (encoded_annotation, encoded_value, recursive arrays)
- [x] Section comments (`# static fields`, `# direct methods`, etc.)
- [x] Parameter register naming (pNN / vNN convention)
- [x] Offset-based branch labels (`:cond_N`, `:goto_N`)
- [x] Interface lists (type_list with uint16 entries)
- [x] ULEB128 / SLEB128 readers
- [x] Pool deduplication (strings, types, protos, fields, methods)
- [x] MUTF-8 string encoding
- [x] Adler32 + SHA-1 checksums
- [x] `const-string-jumbo` 32-bit index support
- [x] Baksmali DEX optimization compat (0x81→int-to-long)
- [x] Try/catch block disassembly — `.catch` / `.catchall` directives in DEX→smali
- [x] Try/catch block assembly — complete try/catch encoding in smali→DEX
- [x] Debug info disassembly — `.line` directives from DEX debug_info_item
- [x] Debug info assembly — `.line`, `.param`, `.local`, `.prologue` encoding in smali→DEX
- [x] Array data payloads — `.array-data` blocks for fill-array-data
- [x] Switch payloads — `.packed-switch` / `.sparse-switch` disassembly and assembly
- [x] Field annotations — per-field annotation blocks in DEX→smali
- [x] Static field initializers — encoded_array_item value emission
- [x] DEX 037+ opcodes — invoke-polymorphic, invoke-custom, const-method-handle, const-method-type (decoder + encoder)

### Not Yet Implemented
- [ ] **Register renumbering** — critical for ART verification (see §13.1)
- [ ] **Parameter annotations** — per-parameter annotation blocks
- [ ] **Method handles / call sites** — DEX 037+ data structures
- [ ] **Sequential labels** — `--sequential-labels` option
- [ ] **Implicit references** — omit class name for same-class refs
- [ ] **Normalized virtual methods** — base-class resolution
- [ ] **Resource ID resolution** — `--resolve-resources`
- [ ] **Accessor comments** — synthetic accessor annotations
- [ ] **Code offset comments** — `--code-offsets`
- [ ] **Register info comments** — type annotations around instructions
- [ ] **Multi-dex support** — `classes2.dex`, `classes3.dex`, etc.
- [ ] **deodex** — oat/odex deodexing
- [ ] **dump** — annotated hex dump
- [ ] **list** — table listing commands
- [ ] **DEX 038/039** — newer DEX format versions
- [ ] **cdex format** — compact DEX (Android P+)
- [ ] **invoke-polymorphic / invoke-custom** — DEX 037+ pool references

---

## 16. Reference Commands

```bash
# Disassemble DEX → smali
java -jar baksmali.jar d classes.dex -o smali_out/

# Disassemble with register renumbering + comments
java -jar baksmali.jar d classes.dex -o smali_out/ --sequential-labels --code-offsets

# Disassemble specific classes
java -jar baksmali.jar d classes.dex -o smali_out/ --classes Lcom/example/Foo;,Lcom/example/Bar;

# Assemble smali → DEX
java -jar smali.jar a smali_out/ -o classes.dex

# Assemble with API level 26
java -jar smali.jar a smali_out/ -o classes.dex -a 26

# List classes in DEX
java -jar baksmali.jar list classes classes.dex

# List strings in DEX
java -jar baksmali.jar list strings classes.dex

# Hex dump DEX
java -jar baksmali.jar dump classes.dex

# Deodex oat file
java -jar baksmali.jar deodex framework.oat -b boot.oat -o out/

# Agent-EJ native equivalents
./agent-ej tool read_dex path=classes.dex out_dir=smali_out/     # disassemble
./agent-ej tool smali_assemble src_dir=smali_out/ out_dex=out.dex  # assemble
./agent-ej tool analyze_apk path=app.apk                          # analyze
./agent-ej tool disasm_dex path=classes.dex class=0               # single class
```
