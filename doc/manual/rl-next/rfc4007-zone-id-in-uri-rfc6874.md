---
synopsis: "Represent IPv6 RFC4007 ZoneId literals in conformance with RFC6874"
prs: [13445]
---

Prior versions of Nix since [#4646](https://github.com/NixOS/nix/pull/4646) accepted [IPv6 scoped addresses](https://datatracker.ietf.org/doc/html/rfc4007) in URIs like [store references](@docroot@/store/types/index.md#store-url-format) in the textual representation with a literal percent character: `[fe80::1%18]`. This was ambiguous, because the the percent literal `%` is reserved by [RFC3986](https://datatracker.ietf.org/doc/html/rfc3986), since it's used to indicate percent encoding. Nix now requires that the percent `%` symbol is percent-encoded as `%25`. This implements [RFC6874](https://datatracker.ietf.org/doc/html/rfc6874), which defines the representation of zone identifiers in URIs. The example from above now has to be specified as `[fe80::1%2518]`.
