# Agent-EJ Benchmarks

Every number below was measured on this machine, this build, against real `.apk` files — not estimated, not carried over from a different binary, and not compared against a tool we didn't actually run. Where a comparison is made against the official Java toolchain (`baksmali.jar` / `smali.jar` v3.0.10), both sides processed the exact same input file in the exact same way, back to back, on the same machine.

## Methodology

- **Hardware**: Intel Core i5-8365U (4c/8t @ 1.60 GHz), 23 GiB RAM, Linux 6.12 (Debian 13), x86_64.
- **Build**: `zig cc -target x86_64-linux-musl`, `make release` (`-Oz -flto -ffunction-sections -fdata-sections -Wl,--gc-sections -Wl,--strip-all`).
- **Timing/memory tool**: GNU `time -v` (wall clock + `Maximum resident set size`, i.e. peak RSS over the process lifetime — not an average, not an idle reading).
- **Test data**: real, unmodified APKs ranging from 723 KB / 120 classes to 23.6 MB / 17,131 classes. These are the same apps used in the project's [stress-testing round](../CHANGELOG.md) (multidex, ProGuard/R8-shrunk, Kotlin-heavy); file paths are omitted here since they live outside the repo, but app names match the CHANGELOG entries for cross-reference.
- Every number is the median or explicit average of **3 runs**; single-run outliers are not reported.
- Reproduce any row yourself with the exact commands in [Reproducing These Numbers](#reproducing-these-numbers) below.

---

## 1. Binary Size

```
make clean && make          # debug: -O0 -g, unstripped
make clean && make release  # release: -Oz, LTO, --gc-sections, --strip-all
```

| Build | Size | Notes |
|---|---|---|
| **Debug** (`make`) | 6.6 MB | `-O0 -g`, full symbols, no dead-code stripping |
| **Release** (`make release`) | **337 KB** | Static, stripped, `musl` libc, zero dynamic dependencies |

`make pico` (the minimal embedded-CLI target) currently fails to link against this tree's dynamic-skill dispatcher and is excluded from this round's numbers rather than reported with a stale or invented figure — tracked as a build fix, not a benchmark result.

## 2. Idle Process Floor

A single trivial tool call (`./agent-ej tool list_dir path=.`), measured end-to-end including process startup:

| Metric | Value |
|---|---|
| Wall clock | < 1 ms |
| Peak RSS | **748 KB** |

This is the fixed cost of the process itself — no file I/O beyond the invocation, no network, no heap growth. Every number past this point is the *additional* cost of real work.

## 3. Real-World APK Round Trip (decode → rebuild → sign)

`decode_apk` (DEX → smali + AXML → XML + ARSC → XML, in one pass) and `build_apk` (smali → DEX, XML → AXML/ARSC, ZIP, zipalign, JAR v1 + APK v2/v3 signing with a 2048-bit RSA test key) run against four real, differently-sized APKs:

| App | APK size | Classes | `decode_apk` wall | `decode_apk` peak RSS | `build_apk` wall (incl. signing) | `build_apk` peak RSS | Rebuilt size |
|---|---|---|---|---|---|---|---|
| BotX Dialog | 723 KB | 120 | 0.29 s | 3.3 MB | 14.1 s | 7.6 MB | 2.7 MB |
| app-debug | 5.6 MB | 6,688 | 21.1 s | 25.4 MB | 36.4 s | 558 MB | 14 MB |
| gen_signed | 6.9 MB | 7,193 | 22.4 s | 20.2 MB | 37.3 s | 475 MB | 21 MB |
| base (largest tested) | 23.6 MB | 17,131 | 50.7 s | 26.4 MB | 69.9 s | 526 MB | 31 MB |

**Read this honestly, not as a highlight reel:**
- `decode_apk`'s peak RSS stays flat (~20–27 MB) regardless of class count — consistent with the mmap'd, mostly zero-copy disassembly path.
- `build_apk`'s peak RSS does **not** stay flat — it jumps from 7.6 MB at 120 classes to 450–560 MB at 6,000+ classes. This is a known, documented scaling limitation: the string/type/proto pool builder in `smali_pool.c` currently does linear scans for duplicate lookups (an O(n²) cost as the pool grows), tracked as [`AGENTS.md` §7b Phase 5.1](../AGENTS.md) ("hash table for pool lookups"). It is not a leak — RSS is bounded and the process exits cleanly — but it is the single biggest performance gap in the current implementation and the next thing worth fixing before claiming "beta."
- The 14.1 s `build_apk` time on a 120-class app is **not** class-count-dominated — it's the fixed cost of RSA-2048 JAR v1 + APK v2/v3 signing, which barely changes with app size (14.1 s → 69.9 s across a 143x increase in class count is nowhere near linear in classes; it's dominated by the signing step at small sizes and by pool-lookup cost at large ones).

## 4. Head-to-Head vs. the Official Toolchain

Same input file, same machine, back to back: a real 7.4 MB, 7,193-class `classes.dex` extracted from `gen_signed.apk`, run through `baksmali.jar`/`smali.jar` 3.0.10 (the reference Java implementation) and through `agent-ej`.

### Disassemble (DEX → smali)

| Tool | Wall clock (avg of 3) | Peak RSS (avg of 3) |
|---|---|---|
| `baksmali.jar d` | 22.7 s | 348 MB |
| `agent-ej tool read_dex` | 27.8 s | **23.0 MB** |

Agent-EJ is **~1.2x slower** on wall clock for this workload and uses **~15x less memory**. We're reporting the slower number rather than a cherry-picked fast run — see [§5](#5-a-real-bug-this-benchmark-found) for why disassembly isn't the bottleneck worth optimizing first.

### Assemble (smali → DEX)

| Tool | Wall clock | Peak RSS |
|---|---|---|
| `smali.jar a` | 46.5 s | 2.46 GB |
| `agent-ej tool smali_assemble` | **24.3 s** | 474 MB |

Agent-EJ is **~1.9x faster** and uses **~5.2x less memory** assembling the exact same 7,193-class source tree back to a working DEX.

## 5. A Real Bug This Benchmark Found

Running `agent-ej`'s assembler against `baksmali.jar`'s own reference output (rather than only against agent-ej's own disassembler output, which is how most of this project's testing has worked so far) immediately failed with `unknown instruction 'rsub-int'`.

Root cause: Dalvik opcode `0xD1` is spelled `rsub-int` (no suffix) by every real tool — `d8`, `dx`, `baksmali`, `smali` — because it's the one historical exception in the `*-int/lit16` opcode family (`0xD0`-`0xD7`) that dropped its format suffix. Agent-EJ's own assembler *and* disassembler both spelled it `rsub-int/lit16`, so a pure agent-ej round trip (disassemble with agent-ej, reassemble with agent-ej) never caught it — the bug was invisible to every test that only checks internal consistency. It surfaces the moment real-world smali — which `rsub-int` appears in constantly, since it's what compiles from ordinary reversed subtraction like `length - i` — meets the assembler. Fixed in `src/android/smali/smali_optab.h` and `src/android/dex/format_dex_disasm.c`; verified byte-identical against `baksmali.jar`'s mnemonic on the same real class. See [CHANGELOG.md](../CHANGELOG.md) for the full writeup.

This is the reason this file leads with real-toolchain comparisons instead of only internal round-trip numbers: self-consistency hides bugs that only a second, independent implementation exposes.

## Reproducing These Numbers

```bash
# Binary size
make clean && make release && ls -lh agent-ej

# Idle floor
/usr/bin/time -v ./agent-ej tool list_dir path=.

# Real APK round trip
/usr/bin/time -v ./agent-ej tool decode_apk path=<app.apk> out_dir=<src_dir>
/usr/bin/time -v ./agent-ej tool build_apk src_dir=<src_dir> out_apk=<out.apk> key_path=keys/testkey.pem

# Head-to-head vs. the official toolchain (requires baksmali.jar / smali.jar + a JRE)
/usr/bin/time -v java -jar baksmali.jar d classes.dex -o ref_out
/usr/bin/time -v ./agent-ej tool read_dex path=classes.dex out_dir=agentej_out

/usr/bin/time -v java -jar smali.jar a ref_out -o ref_reassembled.dex
/usr/bin/time -v ./agent-ej tool smali_assemble src_dir=ref_out out_dex=agentej_reassembled.dex
```

No numbers in this file are projected, rounded up for effect, or taken from a prior binary — rerun the commands above against the current `agent-ej` build and the current `main` branch to check them yourself.
