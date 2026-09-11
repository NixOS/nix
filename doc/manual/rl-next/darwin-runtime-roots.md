---
synopsis: Nix now finds Darwin runtime garbage collector roots without `lsof`
issues: [3011, 13990]
---

On Darwin, the garbage collector now uses `libproc` to retain store paths
referenced by running processes through their working directories, open files,
mapped regions, or environment variables. This avoids the slow `lsof` fallback
and adds environment-variable roots, which `lsof` could not discover.

macOS does not expose the environment variables of entitled binaries while
System Integrity Protection is enabled, so those roots remain unavailable.
