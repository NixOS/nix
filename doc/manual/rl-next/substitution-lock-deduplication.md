---
synopsis: "Process-safe substitution locking prevents duplicate store path downloads"
prs: []
---

When multiple Nix processes try to substitute the same store path simultaneously
(e.g., when using `nix-eval-jobs` or parallel builds), they now coordinate via
file locks to ensure only one process downloads the NAR from the binary cache
while others wait for it to complete. This prevents duplicate downloads and
reduces load on binary cache servers.

This complements the existing fetcher lock mechanism (which handles `fetchTarball`,
`fetchGit`, etc.) by extending process-safe coordination to binary cache
substitution.

Lock files are stored in `~/.cache/nix/substitution-locks/` and are cleaned up
on successful completion. The flock-based locking mechanism ensures locks are
released even if a process crashes, though the lock files themselves may remain
on disk. Orphan lock files are automatically cleaned up on the next Nix startup
(files older than 24 hours are removed).

A new setting `substitution-lock-timeout` controls how long to wait for the lock
(default: 0 = wait indefinitely). Setting a non-zero timeout allows processes to
proceed with potentially redundant downloads rather than waiting indefinitely.
