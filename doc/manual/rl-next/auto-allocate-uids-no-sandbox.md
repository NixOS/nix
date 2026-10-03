---
synopsis: "`auto-allocate-uids`: fall back to `build-users-group` for non-sandboxed builds"
issues: [9761]
---

When `auto-allocate-uids` is enabled, builds that cannot be sandboxed
(e.g. derivations with `__noChroot = true`, or when `sandbox = false`)
previously ran as an auto-allocated UID with no entry in the host's
`/etc/passwd`. This caused `whoami`, `getpwuid()`, `podman`, `ssh` and
similar tools to fail inside such builds, breaking nixpkgs packages like
`spago-legacy`, `generic-stack-builder` and `xcodeenv`.

Auto-allocated UIDs only make sense inside a user namespace where Nix
fabricates `/etc/passwd`. For non-sandboxed builds, Nix now falls back
to a regular `nixbld` user from `build-users-group` if one is
configured. If neither sandboxing nor `build-users-group` is available,
the build fails with a clear error instead of silently running as an
unknown UID.
