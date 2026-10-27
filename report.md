# Agent-EJ vs Apktool — Feature Comparison

| Feature | Apktool | Agent-EJ | Notes |
|---------|---------|---------|-------|
| **Language** | Java (JAR) | C11 (native binary) | Agent-EJ: no JVM required, ~167KB binary |
| **Platform** | Linux/Mac/Win (Java) | Linux (static musl) | Agent-EJ: single binary, no deps |

## Core Commands

| Feature | Apktool | Agent-EJ | Notes |
|---------|---------|---------|-------|
| CLI interface | `apktool d/b/if` | `agent-ej tool decode_apk/build_apk` | Both support direct tool execution |
| Decode APK | `apktool d app.apk` | `agent-ej tool decode_apk` | ✅ Both |
| Build APK | `apktool b dir/` | `agent-ej tool build_apk` | ✅ Both |
| Framework management | `if`/`cf`/`lf` | ❌ Not supported | Agent-EJ doesn't manage framework APKs |
| Interactive mode | ❌ | ✅ `agent-ej cli` | Full AI agent with memory, tools, Telegram |
| Telegram bot | ❌ | ✅ `agent-ej telegram` | Built-in long-polling daemon |
| Daemon mode | ❌ | ✅ `--daemon` flag | Double-fork background operation |

## Decode/Disassemble

| Feature | Apktool | Agent-EJ | Notes |
|---------|---------|---------|-------|
| AndroidManifest.xml → text | ✅ | ✅ | Agent-EJ resolves `@type/key` refs via ARSC |
| resources.arsc → res/values/*.xml | ✅ | ✅ | Both: strings, bools, colors, ids, styles |
| Config-qualifier directories | ✅ | ✅ | `values-v21/`, `drawable-xhdpi-v4/`, `-night-*` |
| DEX → Smali | ✅ (baksmali) | ✅ (native) | Both 113/113 instruction coverage |
| 9-patch images (npTc → .9.png) | ✅ | ❌ | Agent-EJ doesn't decode 9-patch |
| Assets extraction | ✅ | ✅ | Copied verbatim |
| Native libraries (`lib/`) | ✅ | ✅ | Copied verbatim |
| Original files preserved | `original/` | `original/` | ✅ Both |
| apktool.yml metadata | ✅ | ❌ | Agent-EJ doesn't generate metadata YAML |
| `--no-src` (skip DEX) | ✅ | ❌ | No CLI flag; always disassembles |
| `--no-res` (skip resources) | ✅ | ❌ | No CLI flag; always decodes |
| `--only-manifest` | ✅ | ❌ | No CLI flag; always decodes all |
| `--force` overwrite | ✅ | ❌ | Always overwrites |
| `--jobs` (parallel) | ✅ (up to 8) | ❌ | Single-threaded |
| `--keep-broken-res` | ✅ | ❌ | Fails on resource parse errors |
| `--match-original` | ✅ | ❌ | No `match-original` mode |
| `--no-debug-info` (smali) | ✅ | ❌ | Always includes debug info |

## Build/Assemble

| Feature | Apktool | Agent-EJ | Notes |
|---------|---------|---------|-------|
| Smali → DEX | ✅ (smali.jar) | ✅ (native) | Both produce valid DEX |
| XML → binary AXML | ✅ (aapt2) | ✅ (native) | Agent-EJ: uses `axml_assemble` |
| resources.arsc build | ✅ (aapt2) | ❌ | Agent-EJ keeps original `resources.arsc` |
| 9-patch encode | ✅ (aapt2) | ❌ | Not supported |
| ZIP archive creation | ✅ | ✅ | Agent-EJ: `repack_apk` + zipalign |
| APK signing | ✅ (apksigner) | ✅ (native v1/v2/v3) | Agent-EJ v3 has verification issue #13 |
| Zipalign | ✅ | ✅ | 4-byte alignment |
| `--aapt` custom path | ✅ | ❌ | No external tool support |
| `--copy-original` | ✅ | ❌ | Agent-EJ always builds fresh |
| `--debuggable` injection | ✅ | ❌ | Agent-EJ doesn't modify manifest |
| `--no-crunch` | ✅ | ❌ | Agent-EJ doesn't crunch PNGs |
| `--net-sec-conf` | ✅ | ❌ | No network config injection |

## Binary Format Support

| Feature | Apktool | Agent-EJ | Notes |
|---------|---------|---------|-------|
| ZIP read/write | ✅ | ✅ | Agent-EJ: mmap-based, zero-copy |
| AXML parse | ✅ | ✅ | Full chunk tree |
| AXML encode (round-trip) | ✅ | ✅ | With ARSC reverse lookup |
| resources.arsc parse | ✅ | ✅ | Full package/type/entry/config/attr bags |
| resources.arsc encode | ✅ (aapt2) | ❌ (#5) | Agent-EJ: TOML round-trip only |
| DEX parse/disassemble | ✅ | ✅ | 113/113 opcodes |
| DEX assemble (Smali→DEX) | ✅ | ✅ | With annotations, debug info gaps |
| 9-patch | ✅ | ❌ | Not implemented |
| ELF parser | ❌ | ✅ | `re_analyze_elf` tool |
| ADB protocol | ❌ | ✅ | Native ADB, no `adb` binary needed |

## Resource Resolution

| Feature | Apktool | Agent-EJ | Notes |
|---------|---------|---------|-------|
| `@ref/0x...` → `@type/key` | ✅ | ✅ | Via ARSC lookup |
| `?ref/0x...` → `?attr/name` | ✅ | ✅ | Via ARSC reverse lookup |
| Enum values (orientation=1→vertical) | ✅ | ✅ | Hardcoded + dynamic ARSC attr bags |
| Flag decomposition (configChanges=0xda0→...) | ✅ | ✅ | Bitwise + gravity nibble-pair |
| Framework attr resolution | ✅ (via 1.apk) | ✅ (hardcoded table) | Apktool uses installed framework; Agent-EJ uses built-in table |
| App-defined custom attrs | ✅ (via ARSC) | ✅ (via ARSC attr bags) | Both parse ResTable_map entries |
| `public.xml` generation | ✅ | ✅ | Full resource ID mapping |
| Styled strings (spans) | ✅ | ❌ | Not parsed |

## Framework Management

| Feature | Apktool | Agent-EJ | Notes |
|---------|---------|---------|-------|
| Install framework APK | ✅ | ❌ | Not supported |
| List frameworks | ✅ | ❌ | Not supported |
| Clean frameworks | ✅ | ❌ | Not supported |
| Built-in framework | ✅ (ships AOSP) | ❌ | No bundled framework |
| `--frame-path` | ✅ | ❌ | No framework directory concept |

## Smali Assembler Gaps

| Feature | Apktool (smali.jar) | Agent-EJ | Notes |
|---------|---------------------|---------|-------|
| Annotation writing | ✅ | ❌ (Phase 2) | Class/field/method/param annotations |
| Debug info writing | ✅ | ❌ (Phase 3) | `.line`, `.locals`, `.prologue`, `.epilogue` |
| `.param` entries | ✅ | ❌ | Parameter name debug info |
| MUTF-8 encoding | ✅ | ❌ (Phase 1) | `\0` → `0xC0 0x80`, surrogate pairs |
| Register range validation | ✅ | ❌ (Phase 4) | Warn on out-of-range registers |
| Static field initializers (all types) | ✅ | ❌ (Phase 1) | Only `int` supported |
| Error reporting (line numbers) | ✅ | ❌ (Phase 5) | Smali parser error messages |

## AI/Agent Features

| Feature | Apktool | Agent-EJ | Notes |
|---------|---------|---------|-------|
| AI agent | ❌ | ✅ | OpenAI-compatible LLM integration |
| Tool-calling loop | ❌ | ✅ | Single tool per turn, feeds back to LLM |
| Session persistence | ❌ | ✅ | Per-chat session save/load |
| Memory/RAG system | ❌ | ✅ | `memorize`/`recall` with auto-injection |
| 60+ built-in tools | ❌ | ✅ | System, git, Termux, RE skills |
| Dynamic skill system | ❌ | ✅ | JSON schema + exec scripts |
| ADB integration | ❌ | ✅ | Native ADB protocol, install/shell/push/pull |
| Telegram bot | ❌ | ✅ | Long-polling daemon |

## Security

| Feature | Apktool | Agent-EJ | Notes |
|---------|---------|---------|-------|
| Path validation in resources | ✅ | ❌ | CVE fix for malicious resource names |
| SAFE_MODE prompting | ❌ | ✅ | Prompts before dangerous operations |
| SANDBOX_RESTRICT | ❌ | ✅ | Restricts file ops to workspace |
| Tool authentication | ❌ | ✅ | Telegram: allowed user IDs filter |
| Validation on build | ✅ (aapt2) | ✅ (native) | DEX validation, pool dedup |

## Summary

| Category | Apktool | Agent-EJ | Winner |
|----------|---------|---------|--------|
| Core APK decode/rebuild | ✅ Full | ✅ Full (w/ gaps) | Apktool (mature) |
| Resource XML generation | ✅ Complete | ✅ Complete | Tie |
| AXML round-trip | ✅ | ✅ | Tie |
| DEX ↔ Smali round-trip | ✅ Complete | ✅ (w/ assembler gaps) | Apktool |
| resources.arsc encode | ✅ (aapt2) | ❌ (#5) | Apktool |
| 9-patch support | ✅ | ❌ | Apktool |
| Framework management | ✅ Complete | ❌ | Apktool |
| Binary size | ~15MB (JAR+JRE) | ~167KB | **Agent-EJ** |
| No dependencies | ❌ (needs Java) | ✅ (static binary) | **Agent-EJ** |
| AI agent + tool ecosystem | ❌ | ✅ | **Agent-EJ** |
| ADB protocol (no adb binary) | ❌ | ✅ | **Agent-EJ** |
| Multiple signing schemes | ✅ (apksigner) | ✅ (native) | Tie (v3 bug) |
