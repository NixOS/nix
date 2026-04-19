---
synopsis: "`builtins.fetchTree` accepts URL-like string arguments without `flakes`"
issues: [5541]
---

Calling `builtins.fetchTree` with a URL-like string argument (for example,
`builtins.fetchTree "github:NixOS/nixpkgs/..."`) no longer requires the
`flakes` experimental feature. The `fetch-tree` experimental feature, which
gates `builtins.fetchTree` itself, is now sufficient.

Indirect references that need to be resolved through the flake registry
(such as the bare `nixpkgs`) still require `flakes` to be enabled, since
the registry is part of the flakes machinery.
