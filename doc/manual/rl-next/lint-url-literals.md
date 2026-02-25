---
synopsis: "New diagnostics infrastructure, with `lint-url-literals` and `lint-short-path-literals` settings"
prs: [15326]
issues: [10048, 10281]
---

A new diagnostics infrastructure has been added for controlling language features that we are considering deprecating.

## [`lint-url-literals`](@docroot@/command-ref/conf-file.md#conf-lint-url-literals)

The `no-url-literals` experimental feature has been stabilized and replaced with a new [`lint-url-literals`](@docroot@/command-ref/conf-file.md#conf-lint-url-literals) setting.

To migrate from the experimental feature, replace:
```
experimental-features = no-url-literals
```
with:
```
lint-url-literals = fatal
```

## [`lint-short-path-literals`](@docroot@/command-ref/conf-file.md#conf-lint-short-path-literals)

The [`warn-short-path-literals`](@docroot@/command-ref/conf-file.md#conf-warn-short-path-literals) boolean setting has been deprecated and replaced with [`lint-short-path-literals`](@docroot@/command-ref/conf-file.md#conf-lint-short-path-literals).

To migrate, replace:
```
warn-short-path-literals = true
```
with:
```
lint-short-path-literals = warn
```

## Setting values

Both settings accept three values:
- `ignore`: Allow the feature without any diagnostic (default)
- `warn`: Emit a warning when the feature is used
- `fatal`: Treat the feature as a parse error

In the future, we may change what the defaults are.
