R""(
# Examples

* Break a lock on a specific store path:

  ```console
  # nix store break-lock /nix/store/abc123-package
  ```

* Preview what locks would be broken without actually breaking them:

  ```console
  # nix store break-lock --dry-run /nix/store/abc123-package
  ```

* Break locks on multiple paths:

  ```console
  # nix store break-lock /nix/store/abc123-package /nix/store/def456-other
  ```

# Description

This command breaks locks on Nix store paths that may be stuck due to
interrupted builds or crashed processes. It attempts to:

1. Find processes holding locks on the specified store paths
2. Kill those processes (unless `--dry-run` is specified)
3. Remove the lock files

This is useful when you encounter messages like "waiting for locks or
build slots" and the build process appears stuck indefinitely.

# Options

* `--dry-run`: Show what locks would be broken without actually breaking
  them. This is useful for seeing which processes would be affected.

# Notes

* This command only works on local Nix stores
* On Linux, it uses `/proc` to identify processes holding locks
* On macOS and other Unix systems, it uses `lsof` to identify processes
* Always prefer letting builds complete naturally or using `Ctrl-C` to
  cleanly interrupt them before resorting to this command
* Breaking locks on paths that are genuinely being built may lead to
  inconsistent store state
)""
