---
synopsis: "Build errors no longer repeat log lines already printed"
issues: [16128]
---

When Nix already printed the build log, for example with `--print-build-logs`, the error for a failed build no longer ends with the last lines of that log.
This now also holds with `--json-log-path`, and through the daemon when both the client and the daemon are from this release or later.
Through the daemon, it also applies to the legacy commands such as `nix-build`, which show build logs unless given `--no-build-output`, and to `--log-format internal-json`.
It does not hold for an `ssh-ng://` store or remote builder.
