<div align="center">
  <h1 align="center">Agent-EJ</h1>
  <p align="center">
    <strong>A zero-dependency, pure-C11 AI agent and native Android reverse-engineering toolkit —<br/>
    decode, edit, and rebuild APKs without Java, JDK, or apktool.</strong>
  </p>
  <p align="center">
    <a href="https://github.com/GrandpaEJ/Agent-EJ/releases"><img alt="version" src="https://img.shields.io/badge/version-v0.10.0-blue"></a>
    <a href="LICENSE"><img alt="license" src="https://img.shields.io/badge/license-GPLv3-blue"></a>
    <img alt="language" src="https://img.shields.io/badge/language-C11-00599C">
    <img alt="dependencies" src="https://img.shields.io/badge/dependencies-zero-brightgreen">
    <img alt="platform" src="https://img.shields.io/badge/platform-Linux%20%7C%20Termux%20(Android)-lightgrey">
    <img alt="status" src="https://img.shields.io/badge/status-alpha-orange">
  </p>
  <p align="center">
    <a href="#key-features">Features</a> •
    <a href="#why-agent-ej">Why Agent-EJ</a> •
    <a href="#quick-start">Quick Start</a> •
    <a href="#apk-decode--rebuild-pipeline">APK Pipeline</a> •
    <a href="#documentation">Documentation</a> •
    <a href="#benchmarks--footprint">Benchmarks</a> •
    <a href="CHANGELOG.md">Changelog</a>
  </p>
</div>

---

## Overview

**Agent-EJ** is an autonomous AI agent *and* a from-scratch Android reverse-engineering suite, both written in pure C11 with **no external runtime dependencies** — no Java, no Python, no JVM, no `libc++`. It compiles to a single static binary and runs comfortably on a $5 VPS, a Debian/Ubuntu server, or directly on an Android phone via Termux.

Most APK tooling (`apktool`, `baksmali`/`smali`, `jadx`) requires a JVM and hundreds of megabytes of dependencies. Agent-EJ implements the same binary formats — **DEX, Smali, AXML, ARSC, ZIP, JAR/APK signing** — natively in C, so the full decode → edit → rebuild → sign workflow runs with a ~3 MB resident set and a binary under 300 KB.

## Why Agent-EJ

- **No JVM required** — decode and rebuild real-world APKs on a headless server, a Docker container, or a phone, with nothing installed beyond the binary itself.
- **No dependency hell** — statically linked, zero shared libraries, zero `pip install`/`npm install` chains.
- **Built for automation** — every capability is a scriptable CLI tool (`./agent-ej tool <name>`) or an LLM-callable function, so it drops straight into CI pipelines or autonomous agent workflows.
- **Stress-tested on real apps** — the Smali/DEX/APK pipeline has been round-trip tested (decode → rebuild → decode) against dozens of real-world obfuscated APKs (ProGuard/R8, Ijiami packer, StringFog string encryption, 76-way multidex, Unicode-obfuscated identifiers) with byte-level content diffing, not just file counts. See [CHANGELOG.md](CHANGELOG.md) for the full bug-hunting history.

## Key Features

### Zero-Java Android Reverse Engineering
A complete, native APK round-trip pipeline isolated inside [`src/android/`](src/android/):

| Format | Capability | Status |
|---|---|---|
| **DEX ⇄ Smali** | Full disassembler and assembler — byte-identical with `baksmali.jar`/`smali.jar` output | ✅ Complete |
| **AXML** | Binary XML decode *and* encode (`AndroidManifest.xml`, layouts, drawables) | ✅ Complete |
| **ARSC** | `resources.arsc` decode to editable `res/values/*.xml`, and re-compile edits back | ✅ Complete |
| **ZIP / APK container** | Extraction, packing, zipalign (4-byte alignment) | ✅ Complete |
| **APK Signing** | JAR v1 (SHA-1/SHA-256, apksigner-compatible digest selection) and APK Signature Scheme v2/v3 | ✅ Complete |
| **ADB client** | Native Android Debug Bridge protocol — install, push/pull, shell, logcat, uninstall | ✅ Complete |
| **Signature killer** | MT/NP-style verification bypass: method stubbing, `PackageInfo`/`SigningInfo` runtime hijack, hardcoded digest rewriting, and native/asset digest patching | ✅ Complete |

The entire pipeline runs **in-process** — no `apktool`, no `jarsigner`, no shelling out to Java — and decodes/rebuilds multidex apps with 10,000+ classes while staying stable across repeated round trips.

### Autonomous AI Orchestration
- **Dynamic Skill Engine** — register new tools at runtime by dropping a JSON schema (`skills/<name>.json`) and an executable script into `skills/`, no recompilation needed.
- **Persistent memory (RAG)** — lightweight file-based keyword store for long-term fact recall (`memorize` / `recall`).
- **Context-cost optimization** — automatically truncates stale tool output from the conversation log, cutting LLM input token costs by up to 70%.
- **Multi-session isolation** — separate, persisted conversation state per CLI session or Telegram `chat_id`.

### High-Performance Daemon Mode
- POSIX double-fork daemonization for long-running Telegram bots (`--daemon`).
- PID tracking (`agent-ej.pid`) and structured JSON diagnostic logs (`agent-ej.log`).

## Quick Start

### Build

```bash
# Release build — optimized, stripped, statically linked (~290 KB)
make release

# Or a minimal embedded build (~80 KB, CLI tools only)
make pico
```

See [Compilation & Installation](#compilation--installation) below for all build targets.

### Run the AI agent

```bash
# Interactive CLI
./agent-ej cli

# Telegram bot, foreground
./agent-ej telegram

# Telegram bot, background daemon
./agent-ej telegram --daemon
kill $(cat agent-ej.pid)   # stop it later
```

### Use the APK/DEX tools directly — no AI required

```bash
# Decode an APK into editable smali + resources
./agent-ej tool decode_apk apk=app.apk out=app_src

# ...edit app_src/smali/**/*.smali or app_src/res/values/*.xml...

# Rebuild, align, and sign in one step
./agent-ej tool build_apk src_dir=app_src out_apk=app_rebuilt.apk key=keys/testkey.pem
```

Run `./agent-ej tools` to list every available tool.

## APK Decode → Rebuild Pipeline

```
app.apk
   │  decode_apk
   ▼
app_src/
├── AndroidManifest.xml      (AXML → human-readable XML)
├── res/values/*.xml         (resources.arsc → editable XML)
├── smali/, smali_classes2/… (classes*.dex → .smali, one dir per DEX)
└── ...assets, lib/, META-INF/ (copied through unchanged)
   │  edit smali / resource XML as needed
   │  build_apk
   ▼
app_rebuilt.apk              (re-assembled, zipaligned, v1+v2/v3 signed)
```

Both directions are implemented natively — disassembly, assembly, resource recompilation, zipalign, and signing all happen inside the same binary with no intermediate Java process.

## Signature Killer (`sigkill`)

Repackaged APKs are signed with a different certificate, so apps that verify their own signature refuse to run. `sigkill` offers the same family of bypasses MT Manager and NP Manager expose, each as a separate mode that can be combined where they do not conflict:

| Mode | Technique |
|---|---|
| `scan` | Report every method touching `PackageManager`/`PackageInfo`/`Signature`/`SigningInfo` and every hardcoded certificate digest |
| `stub` | Replace boolean/int/String/signature-check method bodies with a passing implementation (annotations preserved) |
| `pkg` | Inject `com.agent.ej.SigKiller`, which rewrites `PackageInfo.signatures` and wraps every `Signature[]`/`SigningInfo` read so the app sees the **original** certificate at runtime |
| `const` | Rewrite hardcoded certificate digests (DER hex / `toCharsString`, MD5, SHA-1, SHA-256, base64, Java `hashCode`) from the original to the certificate the APK is actually signed with |
| `native` | Patch the same digest strings inside `lib/**/*.so` and `assets/**` in place (same encoding ⇒ same length) |

`pkg` is the runtime strategy (app keeps seeing the original signature); `const`/`native` are the static strategy (app is taught the new signature). Do not combine those two families for one app — `sigkill` automatically disables `pkg` when `const` is requested.

```bash
# One-shot: decode → patch → rebuild → sign
./agent-ej tool sigkill path=app.apk out=app_killed.apk mode=all

# Runtime hijack only, keep the original cert visible to the app
./agent-ej tool sigkill path=app.apk out=app_killed.apk mode=pkg

# Static rewrite to a new signing certificate, incl. native checks
./agent-ej tool sigkill path=app.apk out=app_killed.apk mode=const,native cert=newcert.pem

# Dry run on a decoded tree — report only
./agent-ej tool sigkill path=app_src mode=scan dry_run=1
```

If `orig=`/`orig_cert=` is omitted the original certificate is taken from the input APK's `META-INF/*.RSA` (PKCS#7), so a normal decode→rebuild flow needs no extra files.


## Configuration (`.env`)

```ini
# Core Configuration
SAFE_MODE=1                 # 1 = Prompt user for dangerous tool actions, 0 = fully autonomous
SANDBOX_RESTRICT=0          # 1 = Strict sandboxing (only reads/writes in current workspace)

# API Connectivity (OpenAI-format compatible)
API_BASE_URL=https://text.pollinations.ai/openai
API_KEY=your-api-key-here   # leave empty if none required
API_MODEL=openai

# Telegram Daemon Settings
TELEGRAM_BOT_TOKEN=your-telegram-token
ALLOWED_TELEGRAM_USER_IDS=12345678,98765432
```

## Compilation & Installation

Agent-EJ builds with `zig cc` (recommended, for painless static cross-compilation) or standard `gcc`/`clang`.

```bash
# Clean previous builds
make clean

# Debug build — full debug info, unoptimized (~6 MB)
make

# Release build — optimized, stripped, static (~290 KB)
make release

# Nano build — single-command compile, no obj/ directory
make nano

# Pico build — minimal embedded CLI, no AI/Telegram features (~80 KB)
make pico
```

## Extending with Custom Skills

Add a new tool in seconds — no recompilation required.

1. **Define the schema** — `skills/get_weather.json`:
   ```json
   {
     "type": "function",
     "function": {
       "name": "get_weather",
       "description": "Get current weather in a city",
       "parameters": {
         "type": "object",
         "properties": {
           "city": { "type": "string", "description": "City name" }
         },
         "required": ["city"]
       }
     }
   }
   ```

2. **Write the executable** — `skills/get_weather.sh`:
   ```bash
   #!/bin/bash
   curl -s "https://wttr.in/${ARG_city}?format=3"
   ```

3. **Make it executable**:
   ```bash
   chmod +x skills/get_weather.sh
   ```

Agent-EJ auto-detects, registers, and executes the tool. LLM-supplied arguments are injected as environment variables prefixed with `ARG_` (e.g. `$ARG_city`).

## Benchmarks & Footprint

Measured against real APKs (not synthetic fixtures), with wall-clock and peak RSS captured via `time -v` — full methodology, raw numbers, and a head-to-head against the official `baksmali.jar`/`smali.jar` toolchain live in **[benchmark/README.md](benchmark/README.md)**.

| Metric | Value |
|---|---|
| **Release binary** | **337 KB** (full feature set, stripped, static `musl`) |
| **Idle process floor** | **748 KB** RSS, < 1 ms |
| **`decode_apk`** (DEX+AXML+ARSC → editable tree) | peak RSS stays **~20–27 MB** from 120 to 17,131 classes |
| **`decode_apk` vs `baksmali.jar`**, same 7,193-class DEX | ~1.2x slower wall clock, **~15x less memory** |
| **`smali_assemble` vs `smali.jar`**, same 7,193-class tree | **~1.9x faster**, **~5.2x less memory** |
| **Largest APK tested** | 23.6 MB / 17,131 classes — decodes in ~51s, rebuilds+signs in ~70s |

`build_apk`'s peak RSS currently scales worse than linear on very large apps (a known pool-lookup bottleneck, tracked in [AGENTS.md](AGENTS.md) §7b Phase 5.1) — [benchmark/README.md](benchmark/README.md) reports this honestly rather than hiding it. The DEX/Smali/APK pipeline is continuously stress-tested against real-world, obfuscated, multi-dex Android applications — see [CHANGELOG.md](CHANGELOG.md) for detailed before/after findings from each testing round.

## Documentation

| Doc | Covers |
|---|---|
| [AGENTS.md](AGENTS.md) | Core architecture & refactoring manifest — memory-first constraints, module layout, the 250-LOC file rule |
| [docs/RE_ARCHITECTURE.md](docs/RE_ARCHITECTURE.md) | Android reverse-engineering internals and design rationale |
| [docs/DEX_SMALI_INTERNALS.md](docs/DEX_SMALI_INTERNALS.md) | DEX format and Smali assembler/disassembler internals |
| [docs/AXML_INTERNALS.md](docs/AXML_INTERNALS.md) | Binary XML (AXML) parser/encoder internals |
| [docs/ARSC_INTERNALS.md](docs/ARSC_INTERNALS.md) | `resources.arsc` format and recompilation internals |
| [docs/APK_SIGNER.md](docs/APK_SIGNER.md) | JAR v1 / APK Signature Scheme v2/v3 signing internals |
| [docs/CLI_TOOLS_GUIDE.md](docs/CLI_TOOLS_GUIDE.md) | Full CLI tool reference (ADB, APK, ZIP, RE utilities) |
| [docs/AGENT_CORE.md](docs/AGENT_CORE.md) | AI agent loop, skills engine, memory, sessions |
| [CHANGELOG.md](CHANGELOG.md) | Full release history, including every bug found/fixed during stress-testing |
| [CONTRIBUTING.md](CONTRIBUTING.md) | Bug reports, pull requests, and coding guidelines |

## Project Status

Agent-EJ is in **active alpha development**. The DEX/Smali/AXML/ARSC/APK pipeline is functionally complete and has round-tripped dozens of real-world, differently-obfuscated APKs (including 76-way multidex and 17,000+ class applications) with no crashes, no corruption, and byte-stable repeat decodes. It is not yet labeled "beta" — each new, sufficiently different real-world APK has continued to surface edge cases, which [CHANGELOG.md](CHANGELOG.md) documents in full as they're found and fixed. Treat it as a fast-moving, actively-hardened alpha rather than a finished product.

## Contributing

Bug reports, enhancement proposals, and pull requests are welcome — see [CONTRIBUTING.md](CONTRIBUTING.md) for guidelines, and [AGENTS.md](AGENTS.md) for the architectural rules (max 250 LOC per file, no global state, zero-copy parsing) every change must follow.

## License

Agent-EJ is open-source software licensed under the **[GNU General Public License v3.0](LICENSE)**.
