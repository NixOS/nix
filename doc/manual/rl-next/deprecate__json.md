---
synopsis: Deprecate manually making structured attrs with `__json = ...;`
prs: [13220]
---

The proper way to create a derivation using [structured attrs] in the Nix language is by using `__structuredAttrs = true` with [`builtins.derivation`].
However, by exploiting how structured attrs are implementated, it has also been possible to create them by setting the `__json` environment variable to a serialized JSON string.
This sneaky alternative method is now deprecated, and may be disallowed in future versions of Nix.

[structured attrs]: @docroot@/language/advanced-attributes.md#adv-attr-structuredAttrs
[`builtins.derivation`]: @docroot@/language/builtins.html#builtins-derivation
