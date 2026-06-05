---
synopsis: "`nix flake archive` now archives relative `path:` inputs"
issues: [12438]
---

`nix flake archive` previously skipped flake inputs that use a relative path
(e.g. `inputs.x.url = "path:./x"`), so they were neither copied to the store
nor reported in the `--json` output. Such inputs are not independently
fetchable, so the command now copies them from the already-resolved source
trees of the locked flake's node graph — the same data `callFlake` consumes —
instead of re-resolving the relative flakeref. Relative inputs (including
transitively nested ones) now get a store path and are copied to the
destination store.
