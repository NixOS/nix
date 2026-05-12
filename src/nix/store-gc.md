R""(

# Examples

* Delete unreachable paths in the Nix store:

  ```console
  # nix store gc
  ```

* Delete up to 1 gigabyte of garbage:

  ```console
  # nix store gc --max 1G
  ```

* Delete store paths older than 7 days:

  ```console
  # nix store gc --older-than 7d
  ```

# Description

This command deletes unreachable paths in the Nix store.

`--older-than Nd` restricts the GC to only deleting store paths whose most
recent usage is more than *N* days in the past. "Usage" is intentionally defined
ambiguously, however in general all operations which produce/require the
presence of a given store path count as "usage". 

)""
