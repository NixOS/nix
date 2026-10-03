---
synopsis: "Add a `--tree` CLI flag for evaluating a Nix expression from a remote tree"
issues: [10047]
---

New CLI commands (`nix eval`, `nix build`, `nix repl`, ...) and the
legacy `nix-instantiate`, `nix-build` and `nix-shell` commands now
accept a `--tree <tree-ref>` flag. It takes any reference understood by
[`builtins.fetchTree`](@docroot@/language/builtins.md#builtins-fetchTree)
— for example `github:NixOS/nixpkgs` or `git+https://example.com/repo` —
fetches it into the store, and evaluates the `default.nix` inside the
fetched tree. Installables / `--attr` values are resolved as attribute
paths within that expression.

It is the non-flake counterpart of the flake-ref syntax used by the new
CLI and is similar in spirit to running
`nix eval --file "$(nix flake prefetch <tree-ref> --json | jq -r .storePath)" attr.path`,
but without requiring the `flakes` experimental feature. The flag is
gated on the `fetch-tree` experimental feature (which is implied by
`flakes`).

`--tree` is mutually exclusive with `--file` / `--expr` (new CLI), with
`--expr` / `-E`, `-p`, positional file arguments and reading from stdin
(legacy CLI).
