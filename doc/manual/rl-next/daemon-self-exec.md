---
synopsis: Daemon worker spawning rework
issues: [15857]
prs: [16659]
---

Previously, `nix-daemon` accepting on the `nix-daemon.socket` would spawn a worker process by `fork()`-ing without calling `execv*`.
This has historically been a source of issues around fork-unsafe dependencies and in Nix itself.
The most notorious instance of this issue has been Nix on macOS, which discourages such use of fork(2),
and has caused numerous issues in the past (mostly around the use of `SecureTransport` TLS backend in libcurl and aws-c-io).

Now `nix-daemon` spawns a worker processes by spawning and executing itself in `--stdio` mode by redirecting stdin/stdout
to the accepted socket and forcing the trust level with the stabilised `--force-trusted` and `--force-untrusted` flags.
The path to the current binary is either `/proc/self/exe` on Linux, acquired via `_NSGetExecutablePath` on macOS, or `KERN_PROC_PATHNAME` sysctl on FreeBSD.

No functional changes are expected from this, though there are minor user-visible behavioral changes:

- `nix-daemon` no longer needs to be restarted for `/etc/nix/nix.conf` changes to take effect in the daemon *worker*
  (note that this doesn't apply to settings like `allowed-users` or `trusted-users`).
- The command line of the worker process no longer has the form `nix-daemon <peer pid>`.
  Previously, the PID of the daemon client process was inserted into `argv[1]`.
  This also obviates the need for passing the no-op `--daemon` flag for making room for the PID in `argv[1]`.
  The peer PID is now part of the `argv[0]` in the form of `nix-daemon.worker-<peer pid>`.
  This is an internal debugging feature used for correlating the daemon *worker* process with its client.

