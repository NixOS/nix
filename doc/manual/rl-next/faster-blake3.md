---
synopsis: "Faster blake3 hashing"
issues:
prs: [12676]
---

The implementation fro blake3 hashing is now multi-threaded and used memory-mapped IO.
Benchmark results can be found the [pull request](https://github.com/NixOS/nix/pull/12676).
