---
synopsis: Option `allowed-uris` can now match whole schemes in URIs without slashes
prs: 9547
---

If a scheme, such as `github:` is specified in the `allowed-uris` option, all URIs starting with `github:` are allowed.
Previously this only worked for schemes whose URIs used the `://` syntax.
