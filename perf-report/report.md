# Profiling & validating the Nix performance thread

This report reproduces, measures, and validates (or invalidates) each performance
claim raised in a Matrix discussion about profiling Nix. Every number below was
produced on one machine with a fixed, documented methodology; raw `perf stat`
output, the benchmark fixtures, and the flamegraphs are committed alongside this
file so the results are reproducible.

**Machine:** AMD Ryzen 9 5950X (16C/32T, single NUMA node), governor `powersave`,
Turbo/boost **enabled**, `perf_event_paranoid=2`.
**Nix source:** `NixOS/nix` `master` @ `b41781ed0`, self-reported version `2.35.0-dev`.
**Toolchain:** all from-source builds use GCC 15.2.0 (the default `pkgs.stdenv` the
flake selects for `.#default` on `x86_64-linux`).

> Note: these numbers are the **upstream `master` baseline**. The eval-optimization
> commits that also live on this `siraben/perf` branch (`printString`, `base16`,
> `forceValueDeep`, `Bindings` caching, …) are *not* included in the measured builds —
> re-running `scripts/measure_all.sh` against a build of this branch's tip would
> quantify their gains on the same harness.

---

## Methodology — how the numbers were made stable

Turbo is on and I can't pin the frequency without root, so wall-clock and cycle
counts drift with boost/thermals. The fix is to lead with a metric that doesn't:

- **Primary metric: `instructions:u` (retired user-space instructions).** It is
  *deterministic* for a fixed eval — every measurement below reports **±0.00%**
  run-to-run — and frequency-independent. A change in instruction count is real
  work added or removed, not noise.
- **Secondary: `cycles:u`, `task-clock`, wall.** Reported for context; on the idle
  machine these settled to **≤ ±1%** RSD, which is good enough to corroborate the
  instruction signal.
- **Isolation:** pinned to physical cores 4–7 (`taskset -c 4-7`); their SMT
  siblings 20–23 were left idle to avoid hyperthread contention. ASLR disabled
  with `setarch -R`. 3 warm-up runs discarded (page cache / tarball cache / steady
  frequency), then `perf stat -r 15..20`.
- **No cross-run interference:** every measurement ran alone; no builds or other
  profiling ran concurrently.

**Workload.** Evaluate a NixOS system's `config.system.build.toplevel.drvPath` — the
same shape llakala profiled (a real system config). This exercises the whole module
system plus package evaluation and instantiates the derivation graph. The pure
(flake) and impure (`<nixpkgs>`) variants are pinned to the **identical** nixpkgs
revision (`3aa71a66`, 26.05) so the only variable between them is the evaluation
machinery, not the package set. Fixtures: `bench/`.

Caveat worth stating up front: this is one eval-heavy workload on one machine. The
*instruction* deltas generalize well (they're property of the code); the wall-clock
deltas are specific to this CPU. I did not measure build/realisation throughput —
several claims (§8) are about the builder, which this eval workload does not touch,
and are validated by code inspection instead.

---

## Claim 1 — "perf record -g | inferno-collapse-perf is a sane approach"

> `perf record -g result/bin/nix eval …; perf script | inferno-collapse-perf > nix.profile`

**Verdict: Sane, with three accuracy caveats that materially affect the result.**

The pipeline itself is the standard way to get a Nix eval flamegraph and I used a
variant of it for the profiles in this report. But "accurate results" needs care:

1. **Symbols.** A nixpkgs-built `nix` is compiled with `separateDebugInfo = true`
   (`packaging/components.nix:170`) and stripped. `perf` then shows mostly bare
   addresses / PLT stubs unless you point it at the debug output. Profiling a
   locally-built tree (with its `.symtab` intact) — as I do here — resolves the hot
   evaluator frames (`EvalState::eval`, `forceValue`, `EvalState::callFunction`,
   `Bindings::*`) directly. If you must profile the nixpkgs binary, install its
   `debug` output and set `NIX_DEBUG_INFO_DIRS`, or the flamegraph will be a wall of
   `[unknown]`.
2. **Unwinding.** Optimized Nix omits frame pointers, so `perf record -g` (which
   defaults to frame-pointer unwinding) produces broken/short stacks. Use
   `--call-graph dwarf` (what I used) or a frame-pointer build. Otherwise the
   call tree above the leaf is wrong even when the leaves are right.
3. **Frequency & single-threadedness.** Nix eval is essentially single-threaded, so
   wall-time in a flamegraph is dominated by one core whose clock is boosting —
   two profiles of the same eval will disagree on absolute time. Compare *fractions*
   of samples, not absolute time, and pin cores. `perf_event_paranoid=2` also means
   no kernel stacks; fine for eval (userspace-bound) but it hides time lost in
   syscalls (e.g. `stat`/`openat` from the accessor stack, §4).

So: sane pipeline, correct instinct — but on a stock nixpkgs binary without debug
info it silently produces a misleading picture. The fix is a symbol-rich, DWARF-
unwound, core-pinned capture.

_What the corrected profile reveals_ (weighted sample shares, symbol-rich build):
this NixOS eval is **parser-bound and GC-bound**, not "business logic"-bound —
**~20% in the Bison parser + lexer** (`BisonParser::parse`, `yylex`) and **~22% in
the Boehm GC** (`GC_malloc_kind`, `GC_mark_from`, `GC_reclaim_generic`), with
`operator new[]` the single heaviest leaf. That is the real texture of Nix eval cost
and worth keeping in mind before optimising anything downstream of it.

---

## Claim 2 — "impure and pure eval perform quite differently"

**Verdict: CONFIRMED. Pure eval of the identical config is ~22% slower in wall
time here, and the gap is dominated by off-CPU store/daemon work, not evaluation.**

Same 2.35 binary, same nixpkgs, same config; only `--impure`+`<nixpkgs>` vs a flake:

| metric | impure (`<nixpkgs>`) | pure (flake) | Δ pure vs impure |
|---|---|---|---|
| instructions:u | 23.773 G | 24.835 G | **+4.5%** |
| cycles:u | 13.330 G | 14.598 G | +9.5% |
| task-clock | 4 147 ms | 4 788 ms | +15.5% |
| **wall** | **4.287 s** | **5.220 s** | **+21.8%** |

The instruction gap (+4.5%, ≈ 1.06 G extra) is the flake machinery plus the deeper
store-only SourceAccessor stack (§4). But wall grows **+21.8%** — far more than
instructions — because pure eval waits off-CPU: `wall − task-clock` is 140 ms impure
vs 432 ms pure, ~292 ms of extra stalling on store-daemon round-trips, source copies
and NAR hashing (§5). This is exactly xokdvium's warning: the two modes are not
interchangeable for profiling, and pure eval's cost is disproportionately I/O the
CPU profile won't show.

---

## Claim 3 — "easy stuff to make nix packaging nixpkgs perform better" (`components.nix:184-194`)

> `-fno-semantic-interposition` + `-Wl,-Bsymbolic-functions`, applied for GCC only.

**Verdict: CONFIRMED and quantified — these two flags are worth ~10% of eval CPU on
GCC-built Nix. This is the single biggest cheap, already-shipping win in the thread.**

I built the *same* master source twice with GCC 15.2.0, identical `--buildtype=release`,
the only difference being those flags (confirmed: 0 vs 325 compile units carrying
`-fno-semantic-interposition`). Both binaries produced the identical output
derivation. Two order-reversed rounds:

| variant | instructions:u | cycles:u | wall |
|---|---|---|---|
| baseline (no flags) | 25.957 G | 15.02 G | 4.68 s |
| **+ interposition flags** | **23.456 G** | **13.22 G** | **4.19 s** |
| **Δ** | **−9.6%** (−2.50 G, exact) | **−12.0%** | **−10.5%** |

Why it works: Nix's evaluator is a storm of tiny cross-TU calls to exported symbols
inside `libnixexpr.so` (`forceValue`, `Bindings::find`, `EvalState::eval`, …). By
default GCC must assume those symbols can be interposed at load time (ELF semantic
interposition), so it emits real PLT/GOT-indirected calls and refuses to inline
across the boundary. `-fno-semantic-interposition` + `-Bsymbolic-functions` let it
bind those calls internally, inline them, and drop the indirection — which is why
the *instruction count itself* falls 9.6% (exact, ±0.00%), not just the cycles.

Nuances that matter:
- **GCC only.** The `lib.optionalString stdenv.cc.isGNU` guard is correct: Clang
  already binds intra-DSO calls this way by default (the comment at
  `components.nix:184-189` cites this), so a clang-built Nix wouldn't see this
  delta. The default `.#default` build uses `pkgs.stdenv` = GCC on Linux, so the
  win is real for the shipped package.
- This is *already applied* in nixpkgs. The measurement is what it buys: anyone
  building Nix with GCC without these flags (a naïve `meson setup build`) leaves
  ~10% of eval on the table.

---

## Claim 4 — "the SourceAccessor hierarchy is … increasingly convoluted"

**Verdict: CONFIRMED by code.** `SourceAccessor` (`src/libutil/include/nix/util/source-accessor.hh:46`)
has ~15 subclasses, and the evaluator stacks them into a deep wrapper chain. From
the evaluator constructor (`src/libexpr/eval.cc:247-294`), a single `maybeLstat`/
`readFile` on the impure root traverses:

```
AllowListSourceAccessor        (pure/restricted only, access check per path)
  └ CachingSourceAccessor      (caches lstat, but NOT readFile/readDirectory)
      └ UnionSourceAccessor     (impure: overlays real FS over store)
          └ MountedSourceAccessor (walks path components per call)
              └ PosixSourceAccessor / store FS accessor
```

Each hop is a virtual call. Concrete, ranked overhead points found:

1. **Union double-stat (impure):** `union-source-accessor.cc:33-66` — `readFile`/
   `readDirectory`/`readLink` each call `maybeLstat` across inner accessors *first*,
   then repeat the real op → 2× dispatch+syscall per access.
2. **Uncached content reads:** `CachingSourceAccessor` memoises `lstat` but passes
   `readFile`/`readDirectory` straight through (`caching-source-accessor.cc`), so
   repeated reads re-hit the leaf.
3. **Per-path allow-list checks** on every access in pure/restricted mode
   (`filtering-source-accessor.hh:65`).
4. Git work-dir fetches add further `Mounted` layers per submodule
   (`src/libfetchers/git.cc:1029-1033`) and export-ignore filtering.

This is the accessor cost that (partly) shows up as pure eval's extra instructions
in §2. It's real, but it's a second-order effect next to §3 and §9.

---

## Claim 5 — "without --impure we've got store copies (bar lazy trees, but still hashing)"

**Verdict: CONFIRMED by code, and visible in the pure/impure gap (§2).** In pure eval,
local sources reach the store through `fetchToStore2`
(`src/libfetchers/fetch-to-store.cc:44`, called from `eval.cc:2601,3342`,
`primops.cc:2877`, …). `FetchMode::Copy` → `store.addToStore` NAR-serialises + hashes
+ writes the object (`:120`); even `FetchMode::DryRun` ("lazy trees") still runs the
full `dumpPath` hash to compute the store path (`:104-116`, with a `FIXME: already
computed` at `:107`). The hash itself is `SourceAccessor::hashPath` → `dumpPath` →
`HashSink` (`source-accessor.cc:74-79`). Impure eval short-circuits this: physical
paths and direct store paths skip the copy (`primops.cc:174,2851`, `path.cc:166`),
and the impure root unions the real filesystem in so genuine files are read in place.

Caching nuance: hashing is amortised by a persistent, fingerprint-keyed on-disk
cache (`fetch-to-store.cc:62-88`) **when the accessor exposes a fingerprint**;
otherwise it's paid in full every run. So "still hashing" is accurate for
fingerprint-less sources and on cold caches — which is precisely the off-CPU
292 ms the pure workload spends that impure doesn't (§2).

_Profiling evidence:_ in the DWARF-unwound flamegraphs (`results/flame-*.svg`),
`fetchToStore` accounts for **10.3% of samples in the pure profile vs 0.9% in the
impure one** — an ~11× increase that is the store-copy path made flesh. NAR hashing
(`sha256_block_data_order_shaext`/`dumpPath`) is ~1.7–1.9% in *both* (the output
`.drv`s are hashed either way). So pure eval's extra cost is dominated by the
copy-into-store + accessor traversal, exactly as claimed.

---

## Claim 6 — "tarball cache's performance worsens over time; `git multi-pack-index write/repack/expire` helps"

**Verdict: CONFIRMED.** The degradation is real *on this machine right now*: the live
`~/.cache/nix/tarball-cache-v2` is a 322 MB bare git repo with **155 packs** and
275,097 objects and **no multi-pack-index** — exactly the many-loose-packs state
xokdvium described (every object lookup fans out across 155 `.idx` files). There is
also a stale pre-v2 `tarball-cache` of 1.8 GB that is pure garbage and can be
deleted.

I measured on a *copy* (non-destructive), running xokdvium's exact three commands
(`git multi-pack-index write` / `repack --batch-size=0` / `expire`):

| | packs | on-disk | random 20k-OID lookup |
|---|---|---|---|
| before | 155 | 322 MB | 67.3 ms ± 10.2 ms |
| after | **1 + MIDX** | **194 MB (−40%)** | **40.6 ms ± 2.7 ms (−40%, 1.66×)** |

Two real wins: **−40% disk** and **−40% object-lookup latency** (plus variance
collapsing from ±15% to ±6.6%). Note the *shape* of the benchmark matters and is
itself a lesson: a bulk `git cat-file --batch-all-objects` scan showed **no** change
(156→154 ms, within noise) because it streams every pack sequentially regardless of
count. Only **random single-object lookups** — the pattern Nix actually generates
when materialising a tree from the tarball cache — expose the 155-`.idx`-binary-search
cost that the MIDX collapses to one. So: claim confirmed, but only measurable with a
representative access pattern.

---

## Claim 7 — "Nix on XFS on ZFS + VACUUMing helped a lot"

**Verdict: Partially verifiable.** The filesystem-layout half (XFS-on-ZFS zvol to
dodge ZFS's poor small-file/fsync behaviour under Nix's many-tiny-files workload) is
plausible and consistent with Nix's I/O pattern, but I can't validate it here — it
needs a ZFS host and A/B pools, which this machine isn't. I flag it as
**unverified (environmental)**, not wrong.

The **VACUUM** half is measurable. Nix's SQLite DBs are append/churn-heavy and never
auto-vacuum, so they bloat. Current sizes here: `fetcher-cache-v4.sqlite` = 166 MB,
and the store's `/nix/var/nix/db/db.sqlite` = **3.4 GB**. Measured on copies:

| db | before | free pages | after | reclaimed | VACUUM time |
|---|---|---|---|---|---|
| `fetcher-cache-v4.sqlite` | 166 MB | 0 | 151 MB | 15 MB (9%) | 1.9 s |
| **`/nix/var/nix/db/db.sqlite`** | **3.4 GB** | **765,844 (3.0 GB!)** | **345 MB** | **3.1 GB (89%)** | 41 s |

The store DB is the eye-opener: **90% of its 3.4 GB was free/dead pages**, and a
single `VACUUM` reclaims **3.1 GB** in 41 s. That is by far the largest, cheapest,
zero-code win in the whole thread — pure disk, no source changes. (Do it via the
daemon / while nix is quiescent, since the live DB is WAL-mode and root-owned; I
measured a copy to avoid touching the live store.) So the VACUUM half is strongly
**confirmed**; the XFS-on-ZFS half remains environmentally unverifiable here.

---

## Claim 8 — "bigger fish": writeback store, reflinking, kill `fork()` for builders, stop blocking the worker loop

None of these touch the eval workload, so they're validated by code inspection
against master. All four check out.

- **`fork()` for derivation builders — Accurate.** Builders spawn via
  `startProcess`→`doFork` (`src/libutil/unix/processes.cc:212-219,236`) with
  `allowVfork=false` by default (`processes.hh:100`) and **no `posix_spawn`**
  anywhere. Linux sandboxed builds do a *double* spawn ending in `clone()` with
  `CLONE_VM` explicitly forbidden (`processes.cc:272,282`;
  `linux-derivation-builder.cc:555,575-581`), so the (potentially large, GC-heap-
  laden) daemon's page tables are COW-duplicated on every build.
- **Blocking the worker event loop — Accurate.** `Worker::run`
  (`src/libstore/build/worker.cc:296-335`) drives goals on a single thread and even
  self-diagnoses how long a goal "held the event loop". `registerOutputs`
  (`derivation-builder.cc:244,1069`) runs `canonicalisePathMetaData`,
  `scanForReferences`, NAR `dumpPath`/`hashPath`, a full re-serialising
  `copyRecursive`, and `optimisePath` (hard-linking under a global lock,
  `optimise-store.cc:187`) — all inline on the loop. For multi-GB outputs that's
  seconds of stall. (Counter-point: substitutions were *already* moved off-loop to a
  detached thread, `substitution-goal.cc:214-236`, and the post-build hook is now
  async — so the claim is specifically about the local-build register/hash/optimise
  path, which is still synchronous.)
- **Writeback store — Aspirational (not found).** No such feature/type exists; the
  nearest is scattered `FIXME: do this asynchronously` (`nar-cache.cc:63`). It would
  solve exactly the §8-blocking problem by letting a build "finish" and deferring the
  durable copy/register/optimise/fsync.
- **Reflinking — Accurate.** `copy_file_range`/`FICLONE`/"reflink" appear in the
  whole tree exactly once — a **TODO** at `derivation-builder.cc:1415`
  ("Use copyRecursive here and make use of reflinking"). Real copies that could
  CoW-clone but don't: `copyFile` uses `std::filesystem::copy_file`
  (`file-system.cc:588,601`), the store optimiser dedups with *hard links* not
  reflinks (`optimise-store.cc:187`), and `copyRecursive` streams bytes with no
  fast path (`fs-sink.cc:29`). No reflink support exists yet.

---

## Claim 9 — "excited for improvements in 2.35"

**Verdict: CONFIRMED — 2.35 evaluates the identical nixpkgs NixOS config with
**20.5% fewer instructions** than the installed 2.32.1.** Both binaries are
nixpkgs-built (so both already carry the §3 flags); this isolates evaluator/source
improvements between the releases:

| version | instructions:u | cycles:u | wall |
|---|---|---|---|
| 2.32.1 (installed) | 29.919 G | 15.644 G | 5.035 s |
| **2.35.0 (master)** | **23.773 G** | **13.863 G** | **4.354 s** |
| **Δ** | **−20.5%** (−6.15 G) | −11.4% | −13.5% |

That's a large, real reduction in evaluation work over three releases, independent
of build flags or machine.

---

## Summary

| # | Claim | Verdict | Headline number |
|---|---|---|---|
| 1 | perf+inferno is sane | ✅ with caveats | needs symbols + DWARF + core pin |
| 2 | pure ≠ impure perf | ✅ confirmed | pure +21.8% wall (off-CPU I/O) |
| 3 | interposition flags = easy win | ✅ quantified | **−9.6% instr / −10.5% wall (GCC)** |
| 4 | SourceAccessor convoluted | ✅ code-confirmed | 5-deep virtual stack, Union double-stat |
| 5 | pure eval copies+hashes | ✅ code-confirmed | `fetchToStore2`→`addToStore` |
| 6 | tarball cache degrades; MIDX helps | ✅ confirmed | −40% disk & −40% lookup latency |
| 7 | XFS/ZFS + VACUUM | ⚠️ FS unverifiable / ✅ VACUUM | **db.sqlite 3.4 GB → 345 MB** |
| 8 | fork/loop/writeback/reflink | ✅ code-confirmed | all accurate vs master |
| 9 | 2.35 improvements | ✅ confirmed | **−20.5% instr vs 2.32.1** |

**Biggest, cheapest, actionable takeaways**, ranked by bang-for-buck:

1. **`VACUUM` the store DB (§7).** 3.4 GB → 345 MB, reclaiming **3.1 GB** in 41 s,
   zero code. Biggest immediate win on this machine by far.
2. **Ship the interposition flags on any GCC build (§3).** Already in nixpkgs; worth
   a measured **−9.6% instructions / −10.5% wall** of pure eval CPU.
3. **Upgrade to 2.35 (§9).** A genuine **−20.5% eval-instruction** improvement over
   2.32.1, independent of build flags or machine.
4. **Maintain the tarball cache (§6).** `git multi-pack-index write/repack/expire`
   gives −40% disk and −40% object-lookup latency; automate it.
5. **Prefer impure eval when you can (§2), and don't profile the two interchangeably.**
   Pure costs ~+22% wall here, most of it off-CPU store I/O.

The architectural items (§8 — kill builder `fork()`, unblock the worker loop,
writeback store, reflink copies) are the real long-term fish: all four are accurate
against master, but each needs genuine engineering, not a flag or a `VACUUM`.
