R""(

# Examples

* Optimise the Nix store:

  ```console
  nix store optimise
  ```

* Report current dedup savings and predict how much an `optimise`
  pass would save, without modifying the store:

  ```console
  nix store optimise --dry-run
  ```

# Description

This command deduplicates the Nix store: it scans the store for
regular files with identical contents, and replaces them with hard
links to a single instance.

With `--dry-run`, the store is not modified. The command reports
both how much space is already being saved by hard-linking and how
much more would be saved if `optimise` were run now (computed by
hashing every unoptimised file). The same numbers appear under the
`dedup` and `predictedDedup` sections of `nix store stats --json`.

Note that you can also set `auto-optimise-store` to `true` in
`nix.conf` to perform this optimisation incrementally whenever a new
path is added to the Nix store. To make this efficient, Nix maintains
a content-addressed index of all the files in the Nix store in the
directory `/nix/store/.links/`.

)""
