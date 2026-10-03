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

* Clean up deep dependency chains in multiple rounds:

  ```console
  # nix store gc --prune-older-than 3600 --prune-rounds 5
  ```

# Description

This command deletes unreachable paths in the Nix store.

The `--prune-older-than` option performs fast incremental garbage collection by
pruning only leaf paths (paths with no referrers) older than the specified age
in seconds. Unlike full GC which traverses the entire dependency graph, this mode:

- Uses a single SQL query to find deletable leafs
- Only processes the current layer of unused paths per round
- Can be run frequently (e.g., hourly via cron) for incremental cleanup
- Use `--prune-rounds` to run multiple rounds in a single invocation

The `--prune-rounds` option (default 1) runs multiple pruning rounds in a single
invocation. This amortizes the expensive root-finding phase (2-4 seconds on large
stores) across multiple deletion rounds. For example, `--prune-rounds 5` will:
1. Find GC roots once (2-4 seconds)
2. Delete leaf paths (round 1)
3. Delete newly-exposed leafs (round 2)
4. Repeat for rounds 3, 4, 5

This is ideal for maintaining disk space without the overhead of full GC.

)""
