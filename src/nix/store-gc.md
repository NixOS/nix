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

* Fast incremental cleanup of old unused paths:

  ```console
  # nix store gc --prune-older-than 3600
  ```

# Description

This command deletes unreachable paths in the Nix store.

The `--prune-older-than` option performs fast incremental garbage collection by
pruning only leaf paths (paths with no referrers) older than the specified age
in seconds. Unlike full GC which traverses the entire dependency graph, this mode:

- Uses a single SQL query to find deletable leafs
- Only processes the current layer of unused paths
- Can be run frequently (e.g., hourly via cron) for incremental cleanup
- Run multiple times to clean up dependency chains layer by layer

This is ideal for maintaining disk space without the overhead of full GC.

)""
