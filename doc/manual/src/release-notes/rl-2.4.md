# Release 2.4 (202X-XX-XX)

  - It is now an error to modify the `plugin-files` setting via a
    command-line flag that appears after the first non-flag argument
    to any command, including a subcommand to `nix`. For example,
    `nix-instantiate default.nix --plugin-files ""` must now become
    `nix-instantiate --plugin-files "" default.nix`.
  - Plugins that add new `nix` subcommands are now actually respected.
