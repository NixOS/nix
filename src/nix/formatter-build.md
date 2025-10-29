R""(

# Description

`nix formatter build` builds the formatter specified in the flake.

Similar to [`nix build`](@docroot@/command-ref/new-cli/nix3-build.md),
unless `--no-link` is specified, after a successful
build, it creates a symlink to the store path of the formatter. This symlink is
named `./result` by default; this can be overridden using the
`--out-link` option.

It always prints the command to standard output.

# Examples

* Build the formatter:

  ```console
  # nix formatter build
  /nix/store/cb9w44vkhk2x4adfxwgdkkf5gjmm856j-treefmt/bin/treefmt
  ```
)""
