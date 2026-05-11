---
synopsis: "Avoid `flock` self-deadlock when `addToStore` re-enters a path the current build holds"
---

When a build adds a path to the store (for example, a fixed-output or
content-addressed derivation moving its output into place, or an `addToStore`
call that targets the same store path the current `DerivationGoal` already
locked), Nix would call `flock(LOCK_EX)` a second time on a freshly opened
file descriptor for `<path>.lock`. Because Linux `flock` locks are tied to
the open file description rather than the process, the second acquisition
blocks against the first — a self-deadlock that manifests as a build hanging
indefinitely on `waiting for lock on '/nix/store/...'`.

`PathLocks` now tracks which real paths are held by some `PathLocks` instance
in the current process, and the affected call sites in `LocalStore::addToStore`,
`LocalStore::addToStoreFromDump`, and `DerivationBuilderImpl::registerOutputs`
skip the redundant `lockPaths` when the path is already held locally.
