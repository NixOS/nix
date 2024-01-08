R""(

# Examples

* Delete unreachable paths in the Nix store:

  ```console
  # nix store gc
  ```

* Perform garbage collection if there is less than 1 GiB of free space
  in `/nix/store`, and stop once there is at least 5 GiB of free
  space.

  ```console
  # nix store gc --gc-threshold 1G --gc-limit 5G
  ```

# Description

This command deletes unreachable paths in the Nix store, observing the
garbage collection policy configured by the
[`gc-threshold`](@docroot@/command-ref/conf-file.md#conf-gc-threshold) and
[`gc-limit`](@docroot@/command-ref/conf-file.md#conf-gc-limit) configuration
settings. By default, all unreachable paths will be deleted.

)""
