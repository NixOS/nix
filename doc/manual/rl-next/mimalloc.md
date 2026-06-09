---
synopsis: "Link mimalloc for faster evaluation"
prs: [15596]
---

The `nix` binary now links [mimalloc](https://github.com/microsoft/mimalloc)
by default on non-Windows platforms, replacing glibc's malloc for all
non-GC allocations.

This yields a **5–12% wall-clock improvement** on evaluation workloads,
ranging from `nix-instantiate hello` to `nix-env -qa` and full NixOS
configurations.

The allocator can be disabled at build time with `-Dmimalloc=disabled`
or by passing `withMimalloc = false` to the Nix package.
