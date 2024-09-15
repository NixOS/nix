R""(

# Examples

  ```console
  $ readlink foo
  /nix/store/xxx
  $ nix add-root foo
  $ nix-store -q --roots /nix/store/xxx
  .../foo -> /nix/store/xxx
  ```

# Description

This command adds garbage collector root to the paths referenced by the symlinks passed as arguments.
These are called indirect roots, as the root will disappear as soon as the intermediate symlink gets deleted.

)""
