---
synopsis: "Process-safe fetcher cache prevents duplicate downloads"
prs: []
---

When multiple Nix processes fetch the same resource simultaneously (e.g., when
using `nix-eval-jobs`), they now coordinate via file locks to ensure only one
process downloads while others wait for the cached result. This prevents
duplicate downloads and reduces load on remote servers.

The following fetchers now use inter-process locking:
- `tarball` (HTTP/HTTPS tarballs and files)
- `github`, `gitlab`, `sourcehut` (Git forge archives)
- `git` (Git repository fetches)

Lock files are stored in `~/.cache/nix/fetch-locks/` and are cleaned up on
successful completion. The flock-based locking mechanism ensures locks are
released even if a process crashes, though the lock files themselves may
remain on disk. Orphan lock files are automatically cleaned up on the next
Nix startup (files older than 24 hours are removed).

A new setting `fetch-lock-timeout` controls how long to wait for the lock
(default: 0 = wait indefinitely).
