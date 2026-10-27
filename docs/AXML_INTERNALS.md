# Android Binary XML (AXML) Internals

Android Binary XML (AXML) is the format Android uses to store compiled XML files inside an APK (like `AndroidManifest.xml` or layout files in `res/layout/`). It is a highly optimized, binary representation of an XML document.

This document covers how AXML works from a basic concept to advanced internal mechanics, and explains how `Agent-EJ` implements its zero-dependency AXML Encoder.

---

## 1. Basics: Why AXML?

Normal Plaintext XML is human-readable but slow to parse and consumes a lot of space. Android devices need to load app interfaces and permissions instantly. AXML solves this by:
1. **String Deduplication:** Instead of writing the string `"android"` 50 times, it is written exactly once in a "String Pool". Everywhere else, the XML uses a simple 4-byte integer index to refer to that string.
2. **Zero-Copy Parsing:** AXML is designed to be loaded directly into memory (`mmap`). The Android OS doesn't need to allocate objects to read the XML; it can just cast memory pointers directly to C/C++ structs.
3. **Pre-Parsed Data Types:** If an XML attribute is `<activity android:exported="true">`, the string `"true"` is converted at compile time into a binary Boolean type (`TYPE_INT_BOOLEAN` with data `0xFFFFFFFF`). This saves the CPU from doing string-to-boolean conversions at runtime.
4. **Resource ID Mapping:** System attributes (like `android:label`) are mapped to internal Android integer identifiers (e.g., `0x01010001`). This makes framework attribute lookups extremely fast ($O(1)$ integer comparisons instead of string comparisons).

---

## 2. Advanced: The File Structure

An AXML file is composed of sequentially placed "Chunks". Each chunk has a standard 8-byte header:
- `uint16_t type`: Identifies the chunk type (e.g., String Pool, Element Start).
- `uint16_t header_size`: Size of the header itself.
- `uint32_t size`: Total size of the chunk (header + data).

### The Chunk Sequence
A standard `AndroidManifest.xml` compiled into AXML looks like this:

1. **Header (`0x0003`)**: Magic number `0x00080003` indicating an XML file, followed by total file size.
2. **String Pool (`0x0001`)**: Contains all unique strings used anywhere in the XML.
3. **Resource Map (`0x0180`)**: An array of 32-bit integers mapping string indices to official Android Resource IDs.
4. **XML Tree Chunks**: The actual XML nodes.
   - **Start Namespace (`0x0100`)**: Maps a prefix (`android`) to a URI (`http://...`).
   - **Start Element (`0x0102`)**: Represents an opening tag (`<application>`) and contains all its attributes.
   - **End Element (`0x0103`)**: Represents a closing tag (`</application>`).
   - **End Namespace (`0x0101`)**: Closes the namespace scope.

---

## 3. Deep Dive: The String Pool & Resource Map

The String Pool is the most critical part of an AXML file. It stores tags, attribute names, and string values. 

### The Resource Map Constraint
Android's `obtainStyledAttributes` framework (which parses Manifests and Themes) requires attributes to be matched quickly against system definitions. Android achieves this by assigning **Resource IDs** to system attributes. 

For example, the framework knows that the attribute ID for `label` is `0x01010001`. 

The **Resource Map (`0x0180`)** is a flat array of `uint32_t` that corresponds 1-to-1 with the **beginning** of the String Pool.
If the 5th string in the String Pool is `"label"`, the 5th integer in the Resource Map will be `0x01010001`.

**Important Rule:** All strings that represent Android System Attributes MUST be placed at the very beginning of the String Pool. If a string does not have a Resource ID, it goes after this partitioned block.

---

## 4. Deep Dive: START_ELEMENT and Attribute Sorting

The `START_ELEMENT` chunk (`0x0102`) represents an XML tag. It contains:
- The line number.
- The tag's Namespace Index and Name Index.
- A list of attributes.

### How Attributes are Encoded
Each attribute takes exactly 20 bytes:
1. `uint32_t ns_idx`: Index of the namespace URI string (e.g., `android`).
2. `uint32_t name_idx`: Index of the attribute name string (e.g., `label`).
3. `uint32_t val_idx`: Index of the string value (if the value is a string, otherwise `0xFFFFFFFF`).
4. `uint16_t size`: Size of the typed value struct (always 8).
5. `uint8_t res0`: Reserved (0).
6. `uint8_t type`: The Android `Res_value` Data Type (e.g., `0x03` for String, `0x10` for Dec Int, `0x12` for Boolean, `0x01` for Reference).
7. `uint32_t data`: The actual primitive data (e.g., `0xFFFFFFFF` for boolean `true`).

### The AAPT Sorting Rule (Critical)
Inside a `START_ELEMENT` chunk, the array of attributes **MUST be sorted ascending by their Resource ID**.

If an attribute array is unsorted (or sorted just by string names), the Android OS `PackageParser` will silently ignore those attributes. This happens because Android's `obtainStyledAttributes` parses attributes by walking two arrays in parallel (the XML attributes array and the System Theme array) in $O(N)$ time, which mathematically requires both arrays to be strictly sorted by Resource ID.

If an attribute lacks a Resource ID (like `package` or `xmlns`), its Resource ID is treated as `0` and it is sorted logically after or before depending on the AAPT version, usually secondary-sorted by `name_idx`.

---

## 5. Agent-EJ Implementation Strategy

Agent-EJ implements a blazing-fast, zero-dependency AXML Encoder in C11 (`src/android/axml/format_axml_encode.c`) that perfectly mirrors AAPT.

### The Pipeline
1. **Plaintext Parsing (`format_axml_xml_parser.c`)**:
   A custom, minimal recursive-descent parser tokenizes the plaintext XML into an Abstract Syntax Tree (AST).
   
2. **String Collection & Partitioning**:
   The AST is recursively scanned to extract every unique string. 
   Then, the pool is **partitioned**: strings that exist in `axml_res_map.h` (e.g., `label`, `name`, `theme`) are moved to the front. The `Resource Map` chunk is generated based on this partition.

3. **AST Encoding & Attribute Resolution**:
   The AST is walked again. For each tag, attributes are converted from plaintext to binary `Res_value` types. 
   - `"true"` becomes `TYPE_INT_BOOLEAN` -> `0xFFFFFFFF`
   - `"false"` becomes `TYPE_INT_BOOLEAN` -> `0x00000000`
   - `"@ref/0x7f090000"` becomes `TYPE_REFERENCE` -> `0x7f090000`

4. **Attribute Sorting**:
   Before writing the `START_ELEMENT` chunk, the attribute array is sorted ascending by the Resource ID of each attribute's `name_idx`.

5. **Buffer Assembly**:
   All chunks (Header, Pool, ResMap, Namespaces, Elements) are tightly packed into a contiguous `byte_buf` memory block, which is then written to disk.

---

## 6. The Zipalign Requirement

Even if the AXML file is mathematically perfect, Android will fail to parse it (`INSTALL_PARSE_FAILED_UNEXPECTED_EXCEPTION` or silent fallback to default themes) if the APK is not **Zip Aligned**.

Android 11+ enforces that `AndroidManifest.xml` and `resources.arsc` must start at an uncompressed file offset that is a multiple of 4 bytes. This is required because Android uses `mmap()` to mount the APK directly into RAM, and ARM/x86 CPUs require memory pointers for 32-bit integers to be 4-byte aligned.

If you repack an APK using `zip -u`, the offsets change and the APK becomes unaligned. Always run `zipalign -p 4` before signing the APK.
