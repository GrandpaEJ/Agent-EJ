# Agent EJ Pro Benchmarks

Performance metrics, memory baselines, and footprint audits of Agent EJ Pro compiled statically under `x86_64-linux-musl` on Debian Linux.

---

## 1. Binary Footprints (Disk Size)

Statically compiled binaries with all symbols stripped, functional dead-code removal (`--gc-sections`), and link-time optimizations (`-flto -Oz`):

| Target | Executable Size | Included Features |
| :--- | :--- | :--- |
| **`agent-ej-pico32`** | **58 KiB** | 32-bit x86, POSIX CLI, local temp files, memory store |
| **`agent-ej-pico`** | **80 KiB** | 64-bit x64, POSIX CLI, local temp files, memory store |
| **`agent-ej-nano`** | **98 KiB** | 64-bit x64, Interactive CLI, `linenoise` shell history, memory store |
| **`agent-ej`** | **131 KiB** | 64-bit x64, CLI + Telegram Bot, POSIX Daemonizer, memory store |

---

## 2. Memory Consumption (RAM Footprint)

Monitored using `/proc/[pid]/status` resident set size (RSS):

* **Architectural Floor (Static libc/BSS mapping)**: **392 KB**
* **Idle Interactive CLI mode (`agent-ej-pico`)**: **412 KB**
* **Active LLM Processing (69 Tools schema registered)**: **480 KB** 
  *(Includes heap allocations for parsing network HTTP requests and storing temporary JSON nodes using cJSON)*
* **Background Daemon Running (Telegram Polling)**: **436 KB**

*All targets comfortably run within a **< 500 KB RAM** threshold.*

---

## 3. Token & Cost Optimization

To prevent input token bloat, Agent EJ Pro strips large tool responses older than 3 turns. 

### Scenario: Code compilation & Directory Listing
* User requests file list and reads several files.
* **Without Truncation**: Inputs balloon to **4,500+ tokens** after 5 turns because of raw file contents.
* **With Truncation**: Old tool responses are swapped out with a tiny placeholder (`[Tool output truncated for token efficiency]`). Input token count stabilizes at **~1,200 tokens**.
* **Savings**: **~73% input token cost reduction** on long-running loops.

---

## 4. Latency Audits

* **RAG Local Memory Scan**: **< 0.5 ms**
  *(Keyword matches across `.agent_memory.json` flat-file take negligible overhead compared to database bindings)*
* **Dynamic Skill scanning**: **< 1 ms**
  *(Scanning `./skills/*.json` at startup is instantaneous)*
* **Daemon Spawn latency**: **< 3 ms**
  *(Double-fork, SID creation, and PID mapping are executed in CPU microsecond ticks)*

---

## 5. Native Smali Compiler (Zero-Java Pipeline) Benchmark

Performance comparison of assembling and disassembling a Dalvik DEX payload containing 65 complex classes (Android Support compatibility layer):

| Implementation | Execution Time | RAM Footprint | JVM/Runtime Requirement |
| :--- | :--- | :--- | :--- |
| **`smali.jar` (Official Java)** | ~1,200 ms | ~35 MB | Yes (Java Runtime Env) |
| **`agent-ej smali_assemble`** | **< 20 ms** (60x faster) | **< 800 KB** | **None (Pure Static Native)** |

