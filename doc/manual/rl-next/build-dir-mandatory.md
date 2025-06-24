---
synopsis: "`build-dir` no longer defaults to `$TMPDIR`"
---

The directory in which temporary build directories are created no longer defaults
to `TMPDIR` or `/tmp`, to avoid builders making their directories
world-accessible. This behavior allowed escaping the build sandbox and can
cause build impurities even when not used maliciously. We now default to `builds`
in `NIX_STATE_DIR` (which is `/nix/var/nix/builds` in the default configuration).
