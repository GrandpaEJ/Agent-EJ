# ARSC Engine Internals (resources.arsc)

*Note: This feature is currently in Phase 6 of the Agent-EJ implementation plan.*

The `resources.arsc` file is the compiled binary representation of an Android app's resources (strings, styles, layouts, dimensions, and colors). Unlike the DEX or AXML formats, ARSC files are massive nested tables of chunks.

---

## 1. Structure of `resources.arsc`

The binary format is defined by a series of chunks, where each chunk has a standard 8-byte header:
*   `type` (uint16)
*   `header_size` (uint16)
*   `size` (uint32)

### Key Chunks:
1.  **RES_TABLE_TYPE (`0x0002`)**: The root chunk containing the entire table.
2.  **RES_STRING_POOL_TYPE (`0x0001`)**: Contains all the string data used by the resources (e.g., app names, file paths to images).
3.  **RES_TABLE_PACKAGE_TYPE (`0x0200`)**: Contains the resources for a specific package (usually the app's `applicationId`).

---

## 2. Planned Implementation

To maintain Agent-EJ's strict `< 250 LOC` rule and ultra-low RAM footprint, the ARSC module will be implemented using the same **Zero-Copy Iteration** technique used in the DEX disassembler.

### Parsing (`format_arsc_parse.c`)
*   The `resources.arsc` file will be `mmap`'d into memory.
*   Instead of building complex trees of memory-allocated nodes, a `chunk_iterator` will be used to linearly scan through the `type` and `size` headers.
*   Strings will be decoded on-the-fly from the String Pool offsets.

### Editing & Rebuilding (`format_arsc_build.c`)
To change an app's name or swap an icon path, we do not need to rebuild the entire ARSC file from scratch.
*   The builder will implement **In-Place String Patching**.
*   If a new string is shorter or equal in length to the old string, it will be overwritten directly.
*   If it is longer, the String Pool chunk will be dynamically expanded, and the root `size` headers will be updated recursively.

---

## 3. Integration with APK Pipeline

Once implemented, the `build_apk` tool will invoke the ARSC builder to re-link resource IDs and pack the updated `resources.arsc` back into the root of the ZIP file before the alignment and signing phases.
