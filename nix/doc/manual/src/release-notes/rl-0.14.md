# Release 0.14 (2010-02-04)

This release has the following improvements:

  - The garbage collector now starts deleting garbage much faster than
    before. It no longer determines liveness of all paths in the store,
    but does so on demand.

  - Added a new operation, `nix-store --query
                    --roots`, that shows the garbage collector roots that directly or
    indirectly point to the given store paths.

  - Removed support for converting Berkeley DB-based Nix databases to
    the new schema.

  - Removed the `--use-atime` and `--max-atime` garbage collector
    options. They were not very useful in practice.

  - On Windows, Nix now requires Cygwin 1.7.x.

  - A few bug fixes.
