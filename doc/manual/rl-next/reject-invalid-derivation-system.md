---
synopsis: Reject derivations whose `system` contains a backslash or a double quote
issues: 16525
prs: []
---

Nix now rejects a derivation whose [`system`](@docroot@/language/derivations.md#attr-system) attribute contains a backslash (`\`) or a double quote (`"`), and reports a clear error before anything is written to the store.

Previously, such a value was written to the `.drv` file without being escaped, so the file could not be read back.
This showed up as a confusing `error parsing derivation '…': expected string ','` and left an unreadable `.drv` file behind.
The same happened with `nix derivation add`.

With `--readonly-mode`, these values used to produce a store path that does not exist, without any error.
They now fail with the same error as in normal mode.

Other unusual values, such as ones containing spaces or newlines, are unaffected.