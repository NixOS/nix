---
synopsis: Add `repl-overlays` option
prs: 10203
---

A `repl-overlays` option has been added, which specifies files that can overlay
and modify the top-level bindings in `nix repl`. For example, with the
following contents in `~/.config/nix/repl.nix`:

```nix
info: final: prev: let
  optionalAttrs = predicate: attrs:
    if predicate
    then attrs
    else {};
in
  optionalAttrs (prev ? legacyPackages && prev.legacyPackages ? ${info.currentSystem})
  {
    pkgs = prev.legacyPackages.${info.currentSystem};
  }
```

We can run `nix repl` and use `pkgs` to refer to `legacyPackages.${currentSystem}`:

```ShellSession
$ nix repl --repl-overlays ~/.config/nix/repl.nix nixpkgs
Nix 2.21.0pre20240309_4111bb6
Type :? for help.
Loading installable 'flake:nixpkgs#'...
Added 5 variables.
Loading 'repl-overlays'...
Added 6 variables.
nix-repl> pkgs.bash
«derivation /nix/store/g08b5vkwwh0j8ic9rkmd8mpj878rk62z-bash-5.2p26.drv»
```
