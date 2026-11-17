# BNdetC programs

Version **0.1.0** implements ISA and text format **0.1**. Run any tool with `--version` or `--help`. The architectural contract is in the [v0.1 specification](../design/BNdetC_v0_1_Design_Specification.md).

| Tool | Input and output |
|---|---|
| `cnf2isa` | DIMACS CNF → `program.ndp` and `program.map` |
| `ndexec` | Generic assembly image → `result.txt`, `stats.csv` and optional trace |
| `satref` | Original CNF → exhaustive reference result or verification of an executor result |

## Run the bundled x64 programs

Run these commands from `programs/`. Output paths are relative to the current directory.

**Windows PowerShell**

```powershell
.\bin\windows-x86_64\cnf2isa.exe examples/small.cnf
.\bin\windows-x86_64\ndexec.exe program.ndp --backend timed
.\bin\windows-x86_64\satref.exe examples/small.cnf --check result.txt program.map
```

**Ubuntu / Linux**

```sh
chmod +x bin/linux-x86_64/cnf2isa bin/linux-x86_64/ndexec bin/linux-x86_64/satref
./bin/linux-x86_64/cnf2isa examples/small.cnf
./bin/linux-x86_64/ndexec program.ndp --backend timed
./bin/linux-x86_64/satref examples/small.cnf --check result.txt program.map
```

The non-SAT example `examples/subset_sum.ndp` can go directly to `ndexec`. It finds a subset of 3, 5 and 6 whose sum is 8.

## Build from the shared C source

From `programs/`, use `mingw32-make CC=gcc` on Windows with MinGW-w64 or `make` on Linux with GCC. The default output directory is `build/native`. GNU Make accepts `CC=clang` or `OUT=build/another-config` for alternate builds.

Windows x64 binaries use MinGW-w64 GCC 15.1.0 and the system UCRT. Linux x64 binaries were cross-built with Zig 0.17.1 and use a static musl link. The sources require a C17 compiler and the platform's standard console, timing and file interfaces.

## Options and results

- `cnf2isa input.cnf [program.ndp [program.map]]`. Supply a DIMACS header and terminate each clause with `0`. Clause boundaries can occur across or within lines. The declared counts must match the file.
- `ndexec program.ndp`. The default backend is `functional`. Choose `--backend timed` for the discrete-event model. `--slots P --states K` configure the pool, with defaults 4 and 64 and `1 <= P <= K`.
- `--result FILE --stats FILE` select output paths. `--dump-code FILE` writes native instruction indices and 32-bit words in hexadecimal. `--trace FILE` enables a CSV trace with a default limit of 10,000 rows, adjustable with `--trace-limit N`. Truncation is recorded in the statistics.
- Ctrl+C requests cancellation. Linux also accepts SIGTERM. Optional `--timeout-ms N` measures host time after setup, and `--cycle-limit N` requests cancellation at a timed-model boundary after processing completions at that timestamp. The default run has no cutoff.
- `satref input.cnf [--check result.txt program.map] [--max-vars N]`. Independent enumeration defaults to at most 20 variables and accepts a limit from 0 to 63. An ACCEPT witness can be checked without enumerating all assignments. Checking EXHAUSTED runs the independent enumeration.

`ndexec` returns `ACCEPT`, `EXHAUSTED`, `UNKNOWN` or `ERROR`. Exit code 0 means a completed result or successful check, 2 means an incomplete result, and 1 means an error or failed check. An exhausted pool reports `UNKNOWN (STATE_CAPACITY)`. Output paths must be distinct. Each result file is published after its content is complete.

## Image format

The following is a complete text image. `INPUT` declares a word count followed by that many values. `PRIVATE` declares words per branch. Zero or more export directives precede `CODE`.

```text
BNDETC-IMAGE 0.1
ISA 0.1
INPUT 1 7
PRIVATE 0
EXPORT REG R1
CODE
    MOVI R0, 0
    LOAD R1, [R0]
    STOP
```

`EXPORT MEM address count` selects a positive-length range of private logical addresses. Exported registers and addresses appear once in ascending order within their groups. Code accepts labels, the 15 native instructions and four pseudo-instructions from the specification, signed decimal values, and raw hexadecimal words. `#` and `;` start comments. Keywords are case-insensitive and labels are case-sensitive.

A `BNDETC-MAP 0.1` file lists `VARIABLES n`, followed by `variable address` rows in variable order and `END`. A `BNDETC-RESULT 0.1` file lists `STATUS`, `REASON`, `PC` and `HANDLE`, followed by `REG Rn word` or `MEM address word` records and `END`.

## Timing and checks

The timed backend's `reference-1` profile uses two fetch ports, one input port, one private data port and a separate copy port. Fetch, ALU and manager operations take one modeled cycle. Data access and each copy read or write take two. Export reuses the private data port and takes one cycle per register. The CSV records these assumptions, work and traffic counts, resource peaks, waiting and result/cancellation milestones. Modeled cycles and host milliseconds are separate measurements.

Run `python3 tests/test_tools.py bin/linux-x86_64` on Linux or `python tests/test_tools.py bin/windows-x86_64` on Windows. The suite checks both backends against small independent CNF results and exercises the ISA, bounded state pool and cancellation protocol.

The Windows build passed 226 CLI checks, including 30 CNFs on both backends. The Linux files passed static ELF checks. Native Ubuntu execution is pending.
