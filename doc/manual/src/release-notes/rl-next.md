# Release X.Y (202?-??-??)

- Fixed a bug where `nix-env --query` ignored `--drv-path` when `--json` was set.

- Introduced the store [`mounted-ssh-ng://`](@docroot@/command-ref/new-cli/nix3-help-stores.md).
  This store allows full access to a Nix store on a remote machine and additionally requires that the store be mounted in the local filesystem.

- [`nix-shell`](../command-ref/nix-shell.md) shebangs now interpret "-E" with relative paths
