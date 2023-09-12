# Release 2.10 (2022-07-11)

* `nix repl` now takes installables on the command line, unifying the usage
  with other commands that use `--file` and `--expr`. Primary breaking change
  is for the common usage of `nix repl '<nixpkgs>'` which can be recovered with
  `nix repl --file '<nixpkgs>'` or `nix repl --expr 'import <nixpkgs>{}'`.

  This is currently guarded by the `repl-flake` experimental feature.

* A new function `builtins.traceVerbose` is available. It is similar
  to `builtins.trace` if the `trace-verbose` setting is set to true,
  and it is a no-op otherwise.

* `nix search` has a new flag `--exclude` to filter out packages.

* On Linux, if `/nix` doesn't exist and cannot be created and you're
  not running as root, Nix will automatically use
  `~/.local/share/nix/root` as a chroot store. This enables non-root
  users to download the statically linked Nix binary and have it work
  out of the box, e.g.

  ```
  # ~/nix run nixpkgs#hello
  warning: '/nix' does not exists, so Nix will use '/home/ubuntu/.local/share/nix/root' as a chroot store
  Hello, world!
  ```

* `flake-registry.json` is now fetched from `channels.nixos.org`.

* Nix can now be built with LTO by passing `--enable-lto` to `configure`.
  LTO is currently only supported when building with GCC.
