# Agent-EJ Documentation Index

Agent-EJ is a zero-dependency, pure-C11 AI agent and native Android reverse-engineering suite. This directory holds the deep-dive technical documentation; start with the project [README](../README.md) for an overview, quick start, and benchmarks.

## Guides

| Document | Audience | Covers |
|---|---|---|
| [CLI_TOOLS_GUIDE.md](CLI_TOOLS_GUIDE.md) | Users | Every built-in CLI tool — file system, system/process, networking, ADB, APK/ZIP/RE utilities — with copy-pasteable `./agent-ej tool <name> key=val` examples |
| [AGENT_CORE.md](AGENT_CORE.md) | Users & contributors | The AI agent loop, dynamic skill engine, persistent memory, multi-session handling |
| [RE_ARCHITECTURE.md](RE_ARCHITECTURE.md) | Contributors | Android reverse-engineering design rationale and native-C implementation plan |
| [DEX_SMALI_INTERNALS.md](DEX_SMALI_INTERNALS.md) | Contributors | DEX binary format, the Smali disassembler and assembler internals |
| [AXML_INTERNALS.md](AXML_INTERNALS.md) | Contributors | Binary XML (AndroidManifest.xml, layouts) parser and encoder internals |
| [ARSC_INTERNALS.md](ARSC_INTERNALS.md) | Contributors | `resources.arsc` table format, decoding, and recompilation internals |
| [APK_SIGNER.md](APK_SIGNER.md) | Contributors | JAR v1 and APK Signature Scheme v2/v3 signing internals |

## Fastest Path to a Working APK Edit

```bash
# 1. Decode — unpack an APK into editable smali + resource XML
./agent-ej tool decode_apk apk=app.apk out=app_src

# 2. Edit — change app_src/smali/**/*.smali or app_src/res/values/*.xml

# 3. Rebuild — reassemble, zipalign, and sign in one step
./agent-ej tool build_apk src_dir=app_src out_apk=app_rebuilt.apk key=keys/testkey.pem
```

Run `./agent-ej tools` at any time to list every tool the current build registers, including ones added dynamically via the [skills engine](AGENT_CORE.md).

## ADB Quick Reference

```bash
# List connected devices
./agent-ej tool adb_devices

# Install / uninstall an APK
./agent-ej tool adb_install apk_path=/path/to/app.apk
./agent-ej tool adb_uninstall package=com.example.app

# Shell, push, and pull
./agent-ej tool adb_shell command="pm list packages"
./agent-ej tool adb_push local=./file.txt remote=/data/local/tmp/file.txt
./agent-ej tool adb_pull remote=/data/local/tmp/file.txt local=./file.txt
```

Requirements: an ADB server reachable at `localhost:5037` (`adb start-server`), a device connected and authorized, and an RSA key at `~/.android/adbkey` (generated automatically by `adb` on first use).

## AI Agent Modes

```bash
./agent-ej cli                 # Interactive CLI
./agent-ej telegram            # Telegram bot, foreground
./agent-ej telegram --daemon   # Telegram bot, background daemon
```

See [AGENT_CORE.md](AGENT_CORE.md) for session isolation, memory, and the skill-registration format.
