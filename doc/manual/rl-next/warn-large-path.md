---
synopsis: Large path warnings
prs: 10661
---

Nix can now warn when evaluation of a Nix expression causes a large
path to be copied to the Nix store. The threshold for this warning can
be configured using [the `warn-large-path-threshold`
setting](@docroot@/command-ref/conf-file.md#warn-large-path-threshold),
e.g. `--warn-large-path-threshold 100M` will warn about paths larger
than 100 MiB.
