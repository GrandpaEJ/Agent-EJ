# DEX & Smali Engine Internals (dex2smali / smali2dex)

Agent-EJ features a completely native, zero-dependency engine for translating between compiled Android DEX bytecode (`classes.dex`) and human-readable Smali assembly.

This document outlines the architecture, memory management, and technical approach behind both the Disassembler (`dex2smali`) and Assembler (`smali2dex`).

---

## 1. Architecture Overview

The DEX and Smali modules are strictly isolated under `src/android/`:

*   **`src/android/dex/` (Disassembler - dex2smali)**
    *   `format_dex_parse.c`: Maps the DEX binary to memory and builds index tables.
    *   `format_dex_smali.c`: Orchestrates the class disassembly.
    *   `format_dex_smali_method.c`: Parses method bytecode and handles control flow (try/catch, switch payloads).
    *   `format_dex_smali_annot.c`: Decodes LEB128 annotations and encoded values.
    *   `format_dex_smali_util.c`: Shared helpers for formatting and escaping strings.

*   **`src/android/smali/` (Assembler - smali2dex)**
    *   `smali_lexer.c`: Tokenizes raw `.smali` text into registers, strings, and keywords.
    *   `smali_parser.c`: Constructs the class structures (`.class`, `.super`, `.method`).
    *   `smali_insn_parser.c`: Looks up instruction opcodes and validates formats.
    *   `smali_encoder.c`: Converts parsed instructions into raw 16-bit DEX words.
    *   `smali_pool.c`: Manages the String, Type, Proto, Field, and Method pools (lexicographically sorted).
    *   `smali_writer.c`: Serializes the DEX binary structure (Header, MapList) and handles alignments.

---

## 2. dex2smali (Disassembler)

### Memory Efficiency via `mmap`
Instead of allocating memory and copying the DEX file piece-by-piece, the engine relies on **Zero-Copy Parsing**.
The file is read into memory once, and the `dex_ctx` merely stores pointers to the offsets. Structures like `dex_header_t` or `dex_class_def_t` are directly cast over the byte buffer, making the RAM footprint near-zero regardless of how large the DEX file is.

### Translation Process
1.  **Header Validation:** Verifies the `dex\n035\0` magic.
2.  **Table Resolution:** Identifies offsets for Strings, Types, and Prototypes.
3.  **Class Def Iteration:** Iterates over the `class_defs` table to process one class at a time.
4.  **Bytecode Decoding:** Methods are dumped by analyzing the `code_item`. Try/catch blocks and debug infos are processed using LEB128 loops to map the original `PC` (Program Counter) to Smali labels (e.g., `:catch_0`, `:cond_1`).

---

## 3. smali2dex (Assembler)

The assembler works in exactly the reverse order but is significantly more complex due to the requirement of strict binary packing and cryptographic sorting.

### Process Flow
1.  **Lexing & Parsing:** Scans a directory of `.smali` files, extracting all method definitions, strings, and types.
2.  **Pool Building:** 
    *   All unique Strings are dumped into a pool.
    *   DEX specification requires the String Pool to be **strictly lexicographically sorted**.
    *   Type, Field, and Method IDs are subsequently generated based on the sorted string indices.
3.  **Encoding:** 
    *   Registers (e.g., `v0`, `p1`) are mapped to 4-bit, 8-bit, or 16-bit indices based on the instruction format (e.g., `21c`, `3rc`).
    *   Outs and Ins registers are calculated.
4.  **Binary Serialization:** 
    *   The `smali_writer.c` serializes the sections (Header → String IDs → Type IDs → ... → Class Data → Code Items).
    *   It ensures a `4-byte` alignment is maintained across sections using padding.
5.  **Hashing & Checksums:**
    *   Calculates the **SHA-1** hash of the entire file (excluding magic & checksum) and stores it in the header.
    *   Calculates the **Adler-32** checksum of the entire file (excluding magic & checksum fields) and stores it.

---

## 4. Multidex Support

Agent-EJ supports automatic multidex resolution.
*   **Decoding:** Multiple `.dex` files (`classes.dex`, `classes2.dex`) are automatically extracted into distinct folders like `smali/` and `smali_classes2/`.
*   **Building:** The compiler will iterate through all `smali*` directories inside the output folder and independently compile them back into their respective `classesN.dex` binaries.

---

## 5. Current Implementation Status

*   **Disassembler (dex2smali):** Fully stable. Byte-identical to `baksmali.jar` for standard class disassembly.
*   **Assembler (smali2dex):** Currently supports full code compilation, string/type deduplication, and multidex formatting. 
*   **Pending (Phase 2):** Annotation blocks (`.annotation`) and static field initializers (complex `encoded_value` arrays) are being actively implemented to achieve 100% round-trip parity.

---

## 6. Usage Guide (Basic to Advanced)

Agent-EJ exposes these native engines directly through its `tool` CLI. Here is how to use them, from basic operations to advanced reverse-engineering techniques.

### Basic Usages

**1. Full APK Decode (Apktool equivalent)**
Decodes an entire APK, parses the `AndroidManifest.xml`, and disassembles all `classes.dex` files into organized `smali/` and `smali_classesN/` directories.
```bash
./agent-ej tool decode_apk path="target.apk" out_dir="/tmp/decoded_apk"
```

**2. Full APK Build & Sign**
Compiles all `smali*` directories back into `classesN.dex` files, repacks the APK, aligns it (`4-byte` zipalign), and signs it using the provided RSA key.
```bash
./agent-ej tool build_apk src_dir="/tmp/decoded_apk" out_apk="rebuilt.apk" key_path="testkey.pem"
```

**3. Assemble Smali to DEX (smali2dex)**
Compile a directory of `.smali` files into a single `.dex` binary.
```bash
./agent-ej tool smali_assemble src_dir="/tmp/smali_source" out_dex="classes.dex"
```

**4. Disassemble DEX to Smali (dex2smali)**
Disassemble an entire `.dex` file into a directory of `.smali` source files.
```bash
./agent-ej tool read_dex path="classes.dex" out_dir="/tmp/smali_out"
```

### Advanced Usages

**1. Target Specific Class Disassembly (Debugging)**
If you are debugging the engine or just need to inspect a single class without dumping the entire DEX file, you can target it by its `class_def` index.
```bash
# Disassemble the class at index 0
./agent-ej tool disasm_dex path="classes.dex" class=0
```

**2. Raw DEX Header & Table Dump**
To analyze the binary structure of a DEX file (Header checksums, Map offsets, String pool sizes) without converting it to Smali.
```bash
# Omitting the 'out_dir' argument triggers raw structural dump
./agent-ej tool read_dex path="classes.dex"
```

**3. Custom APK Resigning Pipeline**
If you manually modified an aligned APK and just want to resign it natively with a custom certificate/key pair:
```bash
./agent-ej tool resign_apk path="aligned.apk" key="mykey.pem" cert="mycert.x509.pem"
```
