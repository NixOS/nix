---
synopsis: Resolve Nix credentials through SecretSpec
---

The new
[`secretspec-access-tokens`](@docroot@/command-ref/conf-file.md#conf-secretspec-access-tokens)
setting maps Git forge hosts and path prefixes to secret names declared in a
`secretspec.toml`.
Nix resolves those names lazily through `secretspec-ffi`, so access-token values
no longer need to be stored in `nix.conf` or exposed by `nix config show`.

[`secretspec-netrc-file`](@docroot@/command-ref/conf-file.md#conf-secretspec-netrc-file)
selects a complete `netrc` secret declared with `as_path = true`, while
[`secretspec-impure-env`](@docroot@/command-ref/conf-file.md#conf-secretspec-impure-env)
maps environment-variable names to inline SecretSpec secrets for fixed-output
derivations. SecretSpec values are resolved only when the corresponding
credential is used.

Nix installs and selects a bundled `secretspec.toml` by default. It declares
optional `GITHUB_TOKEN`, `GITLAB_TOKEN`, `SOURCEHUT_TOKEN`, `NIX_NETRC`, and
`BUILD_TOKEN` secrets, plus a `nix` scope containing all of them. Set
[`secretspec-file`](@docroot@/command-ref/conf-file.md#conf-secretspec-file) to
use a custom manifest with different declarations.

For example:

```ini
secretspec-access-tokens = github.com=GITHUB_TOKEN
secretspec-netrc-file = NIX_NETRC
secretspec-impure-env = PRIVATE_TOKEN=BUILD_TOKEN
secretspec-scope = nix
```

`secretspec-netrc-file` takes precedence over `netrc-file`. Literal
`access-tokens` and `impure-env` entries take precedence over equally specific
or equally named SecretSpec mappings. Resolved values never become part of the
Nix configuration; `nix config show` displays only their SecretSpec names.

On a multi-user daemon, the selected `netrc` is a daemon-wide credential source,
not a per-user one. Users allowed to request builds can cause matching entries
to be used by HTTP(S) transfers, including the `builtin:fetchurl` builder.
Only include credentials intended to be shared across that trust domain.

The [`secretspec-file`](@docroot@/command-ref/conf-file.md#conf-secretspec-file),
[`secretspec-provider`](@docroot@/command-ref/conf-file.md#conf-secretspec-provider),
[`secretspec-profile`](@docroot@/command-ref/conf-file.md#conf-secretspec-profile),
and
[`secretspec-scope`](@docroot@/command-ref/conf-file.md#conf-secretspec-scope)
settings select the SecretSpec resolution context.
Nix links to the `secretspec-ffi` C ABI through its pkg-config metadata and
removes materialized `as_path` files when their resolution context is destroyed.
Support is a build time option (`-Dsecretspec=`, enabled automatically when
`secretspec-ffi` is available); without it the `secretspec-*` settings still
exist but report that Nix was built without SecretSpec support.

Credential-related settings, including `access-tokens`, `impure-env`,
`netrc-file`, and all `secretspec-*` settings, cannot be set from a flake's
`nixConfig`, even when `accept-flake-config` is enabled.
