---
synopsis: "Warning: File-based locking does not work on NFS"
prs: []
---

The inter-process locking mechanism used for fetcher and substitution
deduplication relies on `flock()`, which does not work reliably across
NFS mounts. If your Nix cache directory (`~/.cache/nix/`) is on NFS:

- Multiple processes on different machines may bypass locking entirely
- Duplicate downloads and potential data races may occur
- Lock acquisition may appear to succeed even when another process holds the lock

**Workaround**: Ensure `~/.cache/nix/` is on a local filesystem. You can
configure this by setting the `XDG_CACHE_HOME` environment variable or
by symlinking `~/.cache/nix` to a local directory.

**Technical details**: POSIX `flock()` is advisory and typically not
implemented across NFS mounts. While some NFS implementations support
`fcntl()` byte-range locking, Nix currently uses `flock()` for simplicity
and compatibility with existing code paths.

**Future work**: Consider detecting NFS mounts and warning users, or
migrating to `fcntl()` with byte-range locking for NFS compatibility.
