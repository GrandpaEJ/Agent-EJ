<div align="center">
  <h1 align="center">Agent-EJ 🚀</h1>
  <p align="center">
    <strong>An Ultra-Lightweight, Zero-Dependency Autonomous AI Agent & Native Android Reverse Engineering Suite built in Pure C11</strong>
  </p>
  <p align="center">
    <a href="#-key-features">Key Features</a> •
    <a href="#-quick-start">Quick Start</a> •
    <a href="#-documentation">Documentation</a> •
    <a href="CHANGELOG.md">Changelog</a> •
    <a href="CONTRIBUTING.md">Contributing</a>
  </p>
</div>

---

## ⚡ Overview

**Agent-EJ** is an alpha-stage, autonomous AI agent written from scratch in pure C11. Operating with an extremely tiny RAM footprint (~1.7 MB) and compiling to a static binary with **zero dynamic library dependencies**, it is highly optimized for deployment on VPS, Debian/Ubuntu servers, and Termux (Android).

Beyond being an AI orchestrator, Agent-EJ is also a **Full-Fledged Native Android Reverse Engineering Suite**. It features a high-performance Dalvik Smali compiler and DEX parser, enabling Android APK modifications without requiring any Java/JDK installations.

## 🌟 Key Features

### 🧠 Autonomous AI Orchestration
- **Dynamic Skill Engine**: Register new tools dynamically by dropping JSON schemas (`skills/<name>.json`) and executable scripts into the `skills/` directory—no recompilation needed!
- **Persistent Memory Store (RAG)**: Employs a lightweight file-based keyword database for long-term fact serialization using `memorize` and `recall`.
- **Dynamic Context Truncation**: Automatically strips large tool outputs from logs older than 3 turns to optimize context length, slashing LLM input token costs by up to 70%.
- **Multi-Session Isolation**: Isolates and persists individual chat sessions by CLI and Telegram `chat_id`.

### 🚀 High-Performance Daemonization
- **POSIX Daemonization**: Fully daemonizes background Telegram bots via double-forking (`--daemon` flag).
- **Process Management**: Automatically maps PID files (`agent-ej.pid`) and outputs structured diagnostic JSON logs (`agent-ej.log`) for easy monitoring.

### 📱 Zero-Java Android Reverse Engineering
Agent-EJ includes an embedded, high-performance Dalvik compiler and DEX parser cleanly isolated inside `src/android/`:
- **Smali Assembler**: Assemble `.smali` to Dalvik Executables (`classes.dex`).
- **DEX Disassembler**: Disassemble `classes.dex` back into `.smali` files.
- *(In Progress)*: Native AXML, ARSC, APK Signer, and ZipAlign integration. See [Architecture Roadmap](docs/RE_ARCHITECTURE.md) for details.

## 📖 Documentation

For detailed guides and deep dives into the project's ecosystem, please refer to our sub-documentation files:

- 🏗️ **[AGENTS.md](AGENTS.md)** — Core Architecture & Refactoring Manifest (Memory-First constraints, Semantic Folder Structures).
- 🔄 **[RE_ARCHITECTURE.md](docs/RE_ARCHITECTURE.md)** — Android Reverse Engineering implementation details and roadmaps.
- 📜 **[CHANGELOG.md](CHANGELOG.md)** — Release history and feature additions.
- 🤝 **[CONTRIBUTING.md](CONTRIBUTING.md)** — Pull Request, bug reporting, and coding guidelines.

## ⚙️ Configuration (`.env`)

Configure the agent's behavior via a `.env` file in the workspace root:

```ini
# Core Configuration
SAFE_MODE=1                 # 1 = Prompt user for dangerous tool actions, 0 = fully autonomous
SANDBOX_RESTRICT=0          # 1 = Strict sandboxing (only reads/writes in current workspace)

# API Connectivity (OpenAI Format Compatible)
API_BASE_URL=https://text.pollinations.ai/openai
API_KEY=your-api-key-here   # (Leave empty if none required)
API_MODEL=openai

# Telegram Daemon Settings
TELEGRAM_BOT_TOKEN=your-telegram-token
ALLOWED_TELEGRAM_USER_IDS=12345678,98765432
```

## 🛠️ Compilation & Installation

Build targets using static compilation with `zig cc` (or standard `gcc`):

```bash
# Clean previous builds
make clean

# Debug build (6.2 MB - full debug info, unoptimized)
make        

# Release build (286 KB - optimized, stripped, static)
make release

# Nano build (single-command compile, no obj/)
make nano   

# Pico build (80 KB - minimal embedded CLI)
make pico   
```

## 🚀 Quick Start

### Interactive CLI Mode
```bash
./agent-ej cli
```

### Background Telegram Daemon Mode
To start the Telegram bot in the background:
```bash
./agent-ej telegram --daemon
```
*Note: The background process forks, writes its PID to `agent-ej.pid`, and outputs logs to `agent-ej.log`.*

To gracefully stop the daemon:
```bash
kill $(cat agent-ej.pid)
```

## 🧩 Extending with Custom Skills

Agent-EJ can be extended in seconds. To add a new tool (e.g., `get_weather`):

1. **Create a Schema Definition (`skills/get_weather.json`)**:
   ```json
   {
     "type": "function",
     "function": {
       "name": "get_weather",
       "description": "Get current weather in a city",
       "parameters": {
         "type": "object",
         "properties": {
           "city": {"type": "string", "description": "City name"}
         },
         "required": ["city"]
       }
     }
   }
   ```

2. **Create the Executable Script (`skills/get_weather.sh`)**:
   ```bash
   #!/bin/bash
   curl -s "https://wttr.in/${ARG_city}?format=3"
   ```

3. **Make it Executable**:
   ```bash
   chmod +x skills/get_weather.sh
   ```

Agent-EJ automatically detects, registers, and executes the tool. LLM arguments are injected securely as environment variables prefixed with `ARG_` (e.g., `$ARG_city`).

## 📊 Benchmarks & Footprint

Agent-EJ is engineered for maximum efficiency. Below are the current benchmarks (v0.8.0 release, `make release`) running on a standard Linux environment:

| Metric | Value |
|--------|-------|
| **Static binary** | **293 KB** (full feature set, stripped, static musl) |
| **Base RSS (idle)** | **3.1 MB** |
| **smali_flow — single method** | **5–7 ms** |
| **smali_flow — 165 methods / 41 classes** | **82–97 ms** |
| **disasm_dex — 113 classes → smali files** | **24–37 ms** |
| **smali_assemble — 113 classes → classes.dex** | **197–258 ms** |
| **Peak RSS (smali_assemble)** | **87 MB** |

## 📲 Dalvik & Smali Pipelines

To interact with Android binaries without installing Java:

**Assemble Smali to DEX:**
```bash
./agent-ej tool smali_assemble src_dir=path/to/smali out_dex=path/to/classes.dex
```

**Disassemble DEX to Smali:**
```bash
./agent-ej tool read_dex path/to/classes.dex
```

---

## 📄 License

Agent-EJ is open-source software licensed under the **[GNU General Public License v3.0](LICENSE)**.
