---
synopsis: "`nix run`, `nix search` and `nix bundle` now auto-call functions"
issues: 5316
prs: 15895
---

If an [installable](@docroot@/glossary.md#gloss-installable) given with `--file` or `--expr` evaluates to a function, it is now called automatically, provided each of its arguments has a default value or is passed with `--arg`/`--argstr`.
This makes `nix run`, `nix search` and `nix bundle` behave like `nix build`, `nix shell`, `nix-build` and `nix-env --query`.

For example, `nix run --impure --expr '{ pkgs ? import <nixpkgs> {} }: pkgs.hello'` now works instead of failing with `error: attribute 'type' does not exist`.

`nix eval` and `nix edit`, which show the value itself rather than a package, are unchanged.
