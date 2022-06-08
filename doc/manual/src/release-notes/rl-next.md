# Release X.Y (202?-??-??)

* Nix can now be built with LTO by passing `--enable-lto` to `configure`.
  LTO is currently only supported when building with GCC.

* `nix copy` now copies the store paths in parallel as much as possible (again).
  This doesn't apply for the `daemon` and `ssh-ng` stores which copy everything
  in one batch to avoid latencies issues.
