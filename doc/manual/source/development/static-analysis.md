# Static Analysis

## Introduction

Static analysis tools help detect bugs, security vulnerabilities, and code quality issues without running the code. The Nix project provides on-demand analysis targets at three depth levels, so developers can choose the right trade-off between speed and thoroughness.

## Tool Summary

| Tool | Purpose | Speed | Category |
|------|---------|-------|----------|
| clang-tidy | Linting, bug detection, modernization | Fast | Compile-aware |
| cppcheck | Deep static analysis, undefined behavior | Fast | Compile-aware |
| Clang Static Analyzer | Path-sensitive C++ analysis | Medium | Build-based |
| flawfinder | CWE-oriented security scanning | Very fast | Source-only |
| GCC max-warnings | Additional compiler warnings beyond defaults | Medium | Build-based |
| GCC -fanalyzer | Interprocedural path-sensitive analysis | Slow | Build-based |
| semgrep | Pattern-based security rule matching | Medium | Source-only |
| ASan + UBSan | Runtime memory and undefined behavior detection | Slow | Build+Run |

## Analysis Levels

### Quick (~2-5 minutes)

Best for: before every PR, quick feedback loop.

Includes: **clang-tidy**, **cppcheck**

```console
$ nix build .#analysis-quick --out-link result-quick
$ cat result-quick/summary.txt
```

### Standard (~5-15 minutes)

Best for: before merging, standard quality gate.

Includes: everything in quick + **Clang Static Analyzer**, **flawfinder**, **GCC max-warnings**

```console
$ nix build .#analysis-standard --out-link result-standard
$ cat result-standard/summary.txt
```

### Deep (~20-40 minutes)

Best for: major changes, security-sensitive code, release preparation.

Includes: everything in standard + **GCC -fanalyzer**, **semgrep**, **ASan + UBSan**

```console
$ nix build .#analysis-deep --out-link result-deep
$ cat result-deep/summary.txt
```

## Running Individual Tools

Each tool is available as a separate Nix derivation:

```console
$ nix build .#analysis-clang-tidy      --out-link result-clang-tidy
$ nix build .#analysis-cppcheck        --out-link result-cppcheck
$ nix build .#analysis-clang-analyzer  --out-link result-clang-analyzer
$ nix build .#analysis-flawfinder      --out-link result-flawfinder
$ nix build .#analysis-gcc-warnings    --out-link result-gcc-warnings
$ nix build .#analysis-gcc-analyzer    --out-link result-gcc-analyzer
$ nix build .#analysis-semgrep         --out-link result-semgrep
$ nix build .#analysis-sanitizers      --out-link result-sanitizers
```

Each target produces a `result/` directory containing:
- `report.txt` — human-readable report
- `count.txt` — number of findings (for automated processing)
- Tool-specific formats (e.g., `report.xml` for cppcheck, `report.json` for semgrep)

## Pipeline Structure

The analysis pipeline lives in `packaging/analysis/` with the following modules:

| File | Purpose |
|------|---------|
| `default.nix` | Entry point — imports modules, defines composite targets |
| `compile-db.nix` | Compilation database generation via meson setup |
| `clang-tidy.nix` | clang-tidy runner and derivation |
| `cppcheck.nix` | cppcheck runner and derivation |
| `clang-analyzer.nix` | Clang Static Analyzer via scan-build |
| `flawfinder.nix` | flawfinder runner and derivation |
| `semgrep.nix` | semgrep runner and derivation |
| `semgrep-rules.yaml` | Vendored semgrep rules (47 rules, 9 categories) |
| `gcc.nix` | GCC max-warnings and -fanalyzer builds |
| `sanitizers.nix` | ASan+UBSan via nixComponents.overrideScope |

## Individual Tool Details

### clang-tidy

Runs the [clang-tidy](https://clang.llvm.org/extra/clang-tidy/) linter with the project's `.clang-tidy` configuration. Enabled checker families include:

- **bugprone-\***: Common bug patterns (use-after-move, incorrect sizeof, etc.)
- **cert-\***: CERT C++ secure coding guidelines
- **clang-analyzer-\***: Clang's built-in path-sensitive analyzer
- **cppcoreguidelines-\***: C++ Core Guidelines checks
- **misc-\***: Miscellaneous useful checks
- **modernize-\***: C++ modernization suggestions
- **performance-\***: Performance-related checks
- **readability-\***: Code readability improvements
- **security-\***: Security-related checks

The `HeaderFilterRegex` is set to `src/.*` to include project headers but exclude third-party dependencies.

### cppcheck

Runs [cppcheck](http://cppcheck.net/) with `--enable=all` and `--std=c++20`. Produces both text and XML output. Suppresses `missingInclude` and `unusedFunction` to reduce noise from the modular build structure.

### Clang Static Analyzer

Runs the [Clang Static Analyzer](https://clang-analyzer.llvm.org/) via `scan-build`, wrapping a full meson+ninja build. This performs deeper path-sensitive analysis than clang-tidy's `clang-analyzer-*` aliases, with additional C++-specific checkers:

- `cplusplus.InnerPointer` — dangling inner pointers
- `cplusplus.NewDelete` — mismatched new/delete
- `cplusplus.NewDeleteLeaks` — memory leaks from new
- `cplusplus.PlacementNew` — placement new misuse
- `cplusplus.PureVirtualCall` — calls to pure virtual functions
- Alpha checkers for iterator safety and move semantics

Produces HTML reports and a finding count.

### flawfinder

Runs [flawfinder](https://dwheeler.com/flawfinder/) for CWE-oriented security scanning. Examines source files for calls to potentially dangerous C/C++ functions and maps findings to Common Weakness Enumeration (CWE) identifiers. Uses `--minlevel=1` to report all severity levels.

### GCC Maximum Warnings

Builds the entire project with GCC using an extended set of warning flags:

```
-Wall -Wextra -Wpedantic
-Wformat=2 -Wformat-security
-Wshadow
-Wcast-qual -Wcast-align
-Wwrite-strings -Wpointer-arith
-Wconversion -Wsign-conversion
-Wduplicated-cond -Wduplicated-branches
-Wlogical-op -Wnull-dereference
-Wdouble-promotion -Wfloat-equal
-Walloca -Wvla
-Werror=return-type -Werror=format-security
```

The last two flags (`-Werror=...`) promote critical warnings to build errors. All others are reported as warnings.

### GCC -fanalyzer

Builds with GCC's [`-fanalyzer`](https://gcc.gnu.org/wiki/StaticAnalyzer) flag, which performs interprocedural path-sensitive analysis. This can detect issues like:
- Double-free and use-after-free
- NULL pointer dereferences across function boundaries
- File descriptor leaks
- Tainted data flows

This is significantly slower than regular compilation.

### semgrep

Runs [semgrep](https://semgrep.dev/) with 47 C/C++ rules vendored in `packaging/analysis/semgrep-rules.yaml` (the Nix build sandbox has no network access, so rules cannot be downloaded at build time). The rules are organized into 9 categories:

1. **Unsafe C String/Memory Functions** — `sprintf`, `strcpy`, `gets`, etc.
2. **Memory Management** — raw `malloc`/`free`, `delete this`, suspect `memset`/`memcpy`
3. **Race Conditions / TOCTOU** — `access()`, `chmod()`, `stat()` on pathnames
4. **Type Safety and Casts** — `const_cast`, `reinterpret_cast`, C-style casts
5. **Error Handling** — catch-all without rethrow, empty catch blocks, throw in destructors
6. **Resource Management** — raw `fopen`, `signal()`, `vfork()`, `popen()`
7. **Privilege and Command Execution** — `setuid`, `chroot`, `getenv`, `exec*`
8. **Concurrency** — temporary lock guards, relaxed memory ordering, thread creation
9. **Code Quality / Defensive Programming** — `goto`, side effects in `assert`, dangling `c_str()`

Severity levels: **ERROR** (likely bugs, near-zero false positives), **WARNING** (probable issues worth investigating), **INFO** (patterns to be aware of).

### ASan + UBSan (Sanitizers)

Builds and tests all components with [AddressSanitizer](https://clang.llvm.org/docs/AddressSanitizer.html) (ASan) and [UndefinedBehaviorSanitizer](https://clang.llvm.org/docs/UndefinedBehaviorSanitizer.html) (UBSan) enabled. This is runtime analysis — it detects issues when the test suite exercises the code:

- Buffer overflows and underflows
- Use-after-free, use-after-return
- Integer overflow, shift errors
- Null pointer dereferences
- Alignment violations

## Interpreting Results

Analysis tools produce findings with varying severity levels. Not all findings are bugs — many are style suggestions or potential issues that may be safe in context.

**Prioritize fixing:**
1. Findings from `clang-analyzer-*` and `cert-*` checks (highest confidence)
2. Security findings from flawfinder with CWE identifiers
3. GCC `-fanalyzer` warnings (interprocedural, often real bugs)
4. Any ASan/UBSan runtime errors (always real bugs)
5. Clang Static Analyzer findings (path-sensitive, high confidence)

**Lower priority:**
- `readability-*` and `modernize-*` suggestions
- `performance-*` suggestions (profile before optimizing)
- Low-severity flawfinder hits (many are informational)

## Suppressions

When a finding is a false positive, suppress it at the source:

- **clang-tidy**: `// NOLINTNEXTLINE(check-name)` or `// NOLINT(check-name)`
- **cppcheck**: `// cppcheck-suppress id`
- **flawfinder**: `/* flawfinder: ignore */`

For project-wide suppressions, update `.clang-tidy` or pass `--suppress` flags via the analysis Nix expressions.

## Interactive Use in Dev Shell

The analysis tools are available in the development shell for interactive use:

```console
$ nix develop
$ cppcheck --version
$ flawfinder --version
$ semgrep --version
```

clang-tidy is available via `clang-tools`. GCC `-fanalyzer` is available when using the `native-gccStdenv` shell variant.

## Graduating Checks to CI

When an analysis check is stable (low false-positive rate, consistently passes), consider promoting it to always-on CI:

1. Run the check across several PRs to establish a baseline
2. Fix or suppress existing findings
3. Add the check to pre-commit hooks or CI pipeline
4. Document the promoted check and its suppression patterns
