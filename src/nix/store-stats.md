R""(

# Examples

* Show statistics about the local store:

  ```console
  # nix store stats
  ```

* Show them as JSON:

  ```console
  # nix store stats --json
  ```

# Description

This command shows statistics about the contents of a Nix store:

* `pathCount`: the number of valid store paths.
* `dedup`: what hard-linking (see `auto-optimise-store` and
  `nix store optimise`) already saves, from the entries in the store's
  `.links` directory:
  * `files`: the number of store files that are hard links to an
    entry, beyond the first file for each entry.
  * `bytes`: the size of those files.
* `predictedDedup`: what `nix store optimise` would free. The command
  hashes the files that `nix store optimise` would consider and that
  are not yet hard links to an entry in `.links`, counting each inode
  once:
  * `files`: the number of those files whose contents are already in
    `.links` or in a file hashed before them.
  * `bytes`: the total size of those files.

  These are what `nix store optimise` reports as linked and freed,
  unless it fails to link a file or links one differently. For
  example, it does not count a file whose `.links` entry is corrupted
  (it repairs the entry instead), or files it cannot link because the
  file system is out of space or the file has too many hard links;
  and, when their contents are already in `.links`, it counts both
  names of a single-file store path that is hard-linked to another.

With `--json`, the command prints these fields as a JSON object. A
newer store may add fields.

Hashing makes the command take about as long as `nix store optimise`
on the files that are not yet hard-linked.

Local stores and recent versions of the Nix daemon provide
statistics. Other stores, including local overlay stores, do not.

)""
