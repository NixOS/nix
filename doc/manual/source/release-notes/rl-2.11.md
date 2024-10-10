# Release 2.11 (2022-08-24)

* `nix copy` now copies the store paths in parallel as much as possible (again).
  This doesn't apply for the `daemon` and `ssh-ng` stores which copy everything
  in one batch to avoid latencies issues.
