R""(

# Examples

* Optimise the Nix store:

  ```console
  nix store optimise
  ```

# Description

This command deduplicates the Nix store: it scans the store for
regular files with identical contents, and replaces them with hard
links to a single instance.

Note that you can also set `auto-optimise-store` to `true` in
`nix.conf` to perform this optimisation incrementally whenever a new
path is added to the Nix store. To make this efficient, Nix maintains
a content-addressed index of all the files in the Nix store in the
directory `/nix/store/.links/`.

)""
