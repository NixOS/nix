---
synopsis: "New command `nix store stats` and `nix store optimise --dry-run`"
prs: [15873]
---

The new command [`nix store stats`](@docroot@/command-ref/new-cli/nix3-store-stats.md) reports summary statistics about a Nix store: the number of valid paths, actual disk usage, how much space hard-linking (`auto-optimise-store` / `nix store optimise`) already saves, and how much more a `nix store optimise` run would save.
`--histograms` adds power-of-two size distributions of NAR sizes and of `.links` entries, and `--json` emits the full report.
Statistics are available for local stores, and for daemon stores whose daemon supports them.

`nix store optimise --dry-run` reports the current and predicted hard-linking savings without modifying the store.
