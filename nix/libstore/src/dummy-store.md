R"(

**Store URL format**: `dummy://`

This store type represents a store that contains no store paths and
cannot be written to. It's useful when you want to use the Nix
evaluator when no actual Nix store exists, e.g.

```console
# nix eval --store dummy:// --expr '1 + 2'
```

)"
