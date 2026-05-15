---
synopsis: "Add a build-time `fuzzers` option for libFuzzer harnesses"
issues: [1937]
---

A new Meson feature option, `-Dfuzzers`, builds libFuzzer harnesses
that live alongside the libraries they target. The option is disabled
by default and requires clang. The first harness, `fuzz_url`, exercises
`nix::parseURL`.

When `LIB_FUZZING_ENGINE` is set in the environment (as OSS-Fuzz and
similar drivers do), the harness links against the engine it points at;
otherwise it falls back to `-fsanitize=fuzzer` for local runs under a
clang stdenv.
