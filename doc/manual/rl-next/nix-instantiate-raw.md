---
synopsis: "`nix-instantiate --eval` now supports `--raw`"
prs: [12119]
---

The `nix-instantiate --eval` command now supports a `--raw` flag, when used
the evaluation result must be a string, which is printed verbatim without
quotation marks or escaping.
