---
synopsis: "`nix run` and other commands now auto-call functions with default arguments"
issues: 5316
prs:
---

`nix run`, `nix eval`, and other commands that use `--expr` or `-f` with an expression that evaluates to a function now automatically call that function with default arguments, consistent with `nix shell` and `nix-build`.

For example, `nix run --impure --expr '{pkgs ? import <nixpkgs> {}}: pkgs.hello'` now works as expected.
