# Agent Core Internals

Agent-EJ is not just a collection of reverse-engineering tools—it features a built-in, lightweight **Autonomous AI Orchestrator** designed to interact via HTTP APIs and Telegram Bots.

This document details the architecture of the Core Agent loop and networking capabilities.

---

## 1. Architecture Map

```
src/
├── core/
│   ├── main.c        # Entry point, daemonizes the process if required.
│   ├── agent.c       # The main orchestration and reasoning loop.
│   └── logger.c      # Structured JSON logging for auditing.
└── net/
    ├── http.c        # Polling/Pushing payload to external LLM providers.
    └── telegram.c    # Telegram Bot Long-Polling API.
```

---

## 2. Core Execution Loop (`agent.c`)

When Agent-EJ is started in **daemon mode**, it enters an infinite loop waiting for instructions via its networking interfaces.

### The Lifecycle
1.  **Instruction Reception**: Receives a raw JSON payload (either via a Webhook, Telegram Message, or CLI override).
2.  **Context Building**: The agent gathers local context (memory, current directory state) and formats it into an internal prompt.
3.  **LLM Request (`http.c`)**: 
    *   Agent-EJ sends the payload to a configured LLM provider (defaulted to free Pollinations API if none is set).
    *   The LLM responds with an action array.
4.  **Tool Dispatching (`tool_dispatch.c`)**:
    *   The agent parses the LLM's requested tools (e.g., `"name": "decode_apk", "args": {...}`).
    *   The execution is routed to the respective function in `src/tools/`.
5.  **Feedback Loop**: The result of the tool execution (success or error message) is pushed back into the agent's memory stream, and the loop repeats until the LLM dictates the task is "completed".

---

## 3. Networking Interfaces

### Telegram Bot (`telegram.c`)
Agent-EJ includes a highly optimized Telegram Long-Polling implementation.
*   **Zero-Dependency HTTP**: Uses raw POSIX sockets and native OpenSSL bindings to communicate with `api.telegram.org`.
*   **Offset Management**: Maintains the `update_id` to ensure messages are processed exactly once.
*   **Asynchronous Actions**: When a user commands the bot (e.g., `/analyze app.apk`), the bot downloads the file, queues it to the `agent.c` core, and streams real-time logs back to the Telegram chat.

### Pollinations API / Generic HTTP (`http.c`)
*   Agent-EJ avoids heavy external HTTP libraries like `libcurl` for core communications to keep the binary small.
*   It manually constructs `HTTP/1.1 POST` requests, handles chunked transfer encoding, and parses the raw TCP streams.
*   The `cJSON` library is used to rapidly decode the LLM responses.

---

## 4. State Management and Memory

Because the agent is written in C, memory management during autonomous loops is strictly controlled to prevent leaks over weeks of uptime.

*   **Ephemeral Memory**: Conversation histories and immediate task states are stored dynamically but explicitly freed at the end of a session (`agent_session_free()`).
*   **Persistent Storage (`tool_fs.c`)**: The agent uses the `memorize` and `recall` tools to persist long-term knowledge to the local disk (e.g., API keys, learned file structures).

---

## 5. Security & Sandboxing

Agent-EJ is capable of running raw shell commands (`run_command`) on the host system. To prevent catastrophic failure or malicious prompt-injection:

1.  **Safe Mode**: When enabled, destructive commands (`rm -rf /`, `mkfs`) are automatically rejected.
2.  **Path Jailing**: The file system tools (`read_file`, `write_file`) ensure paths do not resolve outside of the `SANDBOX_RESTRICT` directory using realpath checks.
3.  **Timeout Limits**: Any spawned subprocess is rigidly monitored. If it hangs (e.g., waiting for user input), the agent kills the PID after a defined timeout.
