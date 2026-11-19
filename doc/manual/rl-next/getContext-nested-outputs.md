---
synopsis: "`builtins.getContext` shows nested output contexts without reading the store"
issues: []
prs: []
---

This only affects users of [`dynamic-derivations`], and contexts without nested elements are unchanged.

With that experimental feature, a string can depend on an output of a derivation that is itself an output, by means of [`builtins.outputOf`].
[`builtins.getContext`] now keys such an element by its [innermost constant store path] instead of attempting to resolve it to a store path, and gives the output names from there outward as a list:

```nix
{ "/nix/store/…-hello.drv.drv" = { outputs = [ [ "out" "lib" ] ]; }; }
```

[`builtins.appendContext`] accepts the same form.

[`dynamic-derivations`]: @docroot@/development/experimental-features.md#xp-feature-dynamic-derivations
[`builtins.outputOf`]: @docroot@/language/builtins.md#builtins-outputOf
[`builtins.getContext`]: @docroot@/language/builtins.md#builtins-getContext
[innermost constant store path]: @docroot@/store/derivation/index.md#deriving-path-encoding-higher-order
[`builtins.appendContext`]: @docroot@/language/builtins.md#builtins-appendContext
