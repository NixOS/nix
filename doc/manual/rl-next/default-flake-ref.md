---
synopsis: "Add `default-flake-ref` to resolve bare installable names"
prs: []
---

Bare installable names are now resolved against the new [`default-flake-ref`](@docroot@/command-ref/conf-file.md#conf-default-flake-ref) setting, which defaults to `nixpkgs`. A bare name `<name>` is treated as `<default-flake-ref>#<name>`.

Explicit flake references, paths, store paths and registry entries are unaffected. Set the option to an empty string to disable this behaviour.